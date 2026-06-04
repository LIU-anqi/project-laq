#include "ai_run_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QTextStream>

namespace {
QMutex g_logMutex;
QFile* g_logFile = nullptr;
QtMessageHandler g_previousHandler = nullptr;

QString formatMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QString level = "DEBUG";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO"; break;
    case QtWarningMsg:  level = "WARN"; break;
    case QtCriticalMsg: level = "CRIT"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    QString line = QString("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
             level,
             msg);

    if (context.file && *context.file) {
        line += QString(" (%1:%2)").arg(context.file).arg(context.line);
    }
    return line;
}

void aiMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString line = formatMessage(type, context, msg);
    {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            QTextStream out(g_logFile);
            out.setCodec("UTF-8");
            out << line << "\n";
            out.flush();
        }
    }

    if (g_previousHandler) {
        g_previousHandler(type, context, msg);
    }
}

bool readTriple(const QJsonObject& obj, const QString& key, double out[3])
{
    const QJsonValue v = obj.value(key);
    if (!v.isArray()) {
        return false;
    }
    QJsonArray arr = v.toArray();
    if (arr.size() != 3) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!arr.at(i).isDouble()) {
            return false;
        }
        out[i] = arr.at(i).toDouble();
    }
    return true;
}

bool readPolarities(const QJsonObject& obj, int out[4])
{
    const QJsonValue v = obj.value("polarities");
    if (!v.isArray()) {
        return false;
    }
    QJsonArray arr = v.toArray();
    if (arr.size() != 4) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!arr.at(i).isDouble()) {
            return false;
        }
        out[i] = arr.at(i).toInt();
    }
    return true;
}

QString optionalString(const QJsonObject& obj, const QString& key, const QString& fallback = QString())
{
    const QJsonValue v = obj.value(key);
    return v.isString() ? v.toString() : fallback;
}
}

void AiRunManager::installMessageHandler()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    g_previousHandler = qInstallMessageHandler(aiMessageHandler);
    installed = true;
}

void AiRunManager::setLogFile(const QString& path)
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    g_logFile = new QFile(path);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
    }
}

void AiRunManager::closeLogFile()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

QString AiRunManager::projectRoot()
{
    QStringList starts;
    starts << QDir::currentPath() << QCoreApplication::applicationDirPath();
    for (const QString& start : starts) {
        QDir dir(start);
        while (true) {
            if (dir.exists(".kiro") || dir.exists("CMakeLists.txt")) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return QDir::currentPath();
}

QString AiRunManager::resolvePath(const QString& path, const QString& baseDir)
{
    if (path.isEmpty()) {
        return QString();
    }

    QFileInfo info(path);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString base = baseDir.isEmpty() ? projectRoot() : baseDir;
    return QDir::cleanPath(QDir(base).filePath(path));
}

QString AiRunManager::defaultOutputRoot()
{
    return QDir(projectRoot()).filePath("out/ai_runs");
}

QString AiRunManager::safeToken(QString text, const QString& fallback)
{
    text = text.trimmed();
    text.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    text.replace(QRegularExpression("_+"), "_");
    text = text.trimmed();
    while (text.startsWith("_")) text.remove(0, 1);
    while (text.endsWith("_")) text.chop(1);
    return text.isEmpty() ? fallback : text;
}

QString AiRunManager::timestamp()
{
    return QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
}

bool AiRunManager::loadPlan(const QString& path, AiRunPlan* plan, QString* error)
{
    if (!plan) {
        if (error) *error = "plan output pointer is null";
        return false;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("cannot open plan: %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QString("invalid JSON plan: %1").arg(parseError.errorString());
        return false;
    }

    const QFileInfo planInfo(path);
    const QString rootDir = projectRoot();
    QJsonObject root = doc.object();
    AiRunPlan parsed;
    parsed.raw = root;
    parsed.planId = safeToken(optionalString(root, "planId", planInfo.baseName()), "ai_plan");
    parsed.mainImage = resolvePath(optionalString(root, "mainImage"), rootDir);
    parsed.labelImage = resolvePath(optionalString(root, "labelImage"), rootDir);
    parsed.outputRoot = resolvePath(optionalString(root, "outputRoot", defaultOutputRoot()), rootDir);

    QJsonArray runs = root.value("runs").toArray();
    if (runs.isEmpty()) {
        if (error) *error = "plan has no runs";
        return false;
    }

    for (int i = 0; i < runs.size(); ++i) {
        if (!runs.at(i).isObject()) {
            if (error) *error = QString("run %1 is not an object").arg(i);
            return false;
        }

        QJsonObject obj = runs.at(i).toObject();
        AiRunItem item;
        item.raw = obj;
        item.caseId = safeToken(optionalString(obj, "case", "F1"), "F1").toUpper();
        item.id = safeToken(optionalString(obj, "id",
            QString("%1_%2").arg(i + 1, 3, 10, QLatin1Char('0')).arg(item.caseId)),
            QString("run_%1").arg(i + 1));
        item.presetIndex = obj.value("presetIndex").isDouble()
            ? obj.value("presetIndex").toInt()
            : presetIndexForCase(item.caseId);
        item.nucleiBackfill = obj.value("nucleiBackfill").toBool(false);
        item.exportVtu = obj.value("exportVtu").toBool(true);
        item.hasInputMesh = obj.value("inputMesh").isString();
        if (item.hasInputMesh) {
            item.inputMesh = resolvePath(obj.value("inputMesh").toString(), rootDir);
        }

        item.hasTarget = readTriple(obj, "target", item.target);
        item.hasEntry = readTriple(obj, "entry", item.entry);
        item.hasPolarities = readPolarities(obj, item.polarities);

        item.hasAmplitude = obj.value("amplitude").isDouble();
        if (item.hasAmplitude) item.amplitude = obj.value("amplitude").toDouble();

        item.hasPulseWidth = obj.value("pulseWidth").isDouble();
        if (item.hasPulseWidth) item.pulseWidth = obj.value("pulseWidth").toInt();

        item.hasFrequency = obj.value("frequency").isDouble();
        if (item.hasFrequency) item.frequency = obj.value("frequency").toInt();

        item.hasLeadType = obj.value("leadType").isDouble();
        if (item.hasLeadType) item.leadType = obj.value("leadType").toInt();

        item.hasDepthOffset = obj.value("depthOffset").isDouble();
        if (item.hasDepthOffset) item.depthOffset = obj.value("depthOffset").toDouble();

        item.hasUseEncapsulation = obj.value("useEncapsulation").isBool();
        if (item.hasUseEncapsulation) item.useEncapsulation = obj.value("useEncapsulation").toBool();

        item.hasSigmaEncapsulation = obj.value("sigmaEncapsulation").isDouble();
        if (item.hasSigmaEncapsulation) item.sigmaEncapsulation = obj.value("sigmaEncapsulation").toDouble();

        item.hasEncapsulationThickness = obj.value("encapsulationThickness").isDouble();
        if (item.hasEncapsulationThickness) item.encapsulationThickness = obj.value("encapsulationThickness").toDouble();

        parsed.runs.push_back(item);
    }

    *plan = parsed;
    return true;
}

bool AiRunManager::writeJsonFile(const QString& path, const QJsonObject& obj, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QString("cannot write JSON: %1").arg(path);
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

bool AiRunManager::copyFile(const QString& from, const QString& to, QString* error)
{
    if (from.isEmpty() || to.isEmpty()) {
        if (error) *error = "copyFile got an empty path";
        return false;
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to)) {
        QFile::remove(to);
    }
    if (!QFile::copy(from, to)) {
        if (error) *error = QString("cannot copy %1 to %2").arg(from, to);
        return false;
    }
    return true;
}

int AiRunManager::presetIndexForCase(const QString& caseId)
{
    const QString c = caseId.trimmed().toUpper();
    if (c == "MANUAL") return 0;
    if (c == "F1") return 1;
    if (c == "F2") return 2;
    if (c == "F3") return 3;
    if (c == "F4") return 4;
    if (c == "VIDEODEMO" || c == "VIDEO_DEMO") return 5;
    return -1;
}

QJsonObject AiRunManager::runItemToJson(const AiRunItem& item)
{
    QJsonObject obj = item.raw;
    obj["id"] = item.id;
    obj["case"] = item.caseId;
    obj["presetIndex"] = item.presetIndex;
    obj["nucleiBackfill"] = item.nucleiBackfill;
    obj["exportVtu"] = item.exportVtu;
    if (item.hasInputMesh) {
        obj["inputMesh"] = item.inputMesh;
    }
    return obj;
}
