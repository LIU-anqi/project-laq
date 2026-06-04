#include "dbs_mesh_worker.h"

// VTK
#include <vtkNew.h>
#include <vtkExtractVOI.h>
#include <vtkImageCast.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelectEnclosedPoints.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkImageThreshold.h>
#include <vtkImageConstantPad.h>
#include <vtkFillHolesFilter.h>
#include <vtkCleanPolyData.h>
#include <vtkConnectivityFilter.h>
#include <vtkCellArray.h>
#include <vtkIdList.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyDataNormals.h>

// CGAL (条件编译，等库安装后启用)
#ifdef DBS_HAS_CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Mesh_triangulation_3.h>
#include <CGAL/Mesh_complex_3_in_triangulation_3.h>
#include <CGAL/Mesh_criteria_3.h>
#include <CGAL/Labeled_mesh_domain_3.h>
#include <CGAL/Polyhedral_complex_mesh_domain_3.h>
#include <CGAL/Mesh_polyhedron_3.h>
#include <CGAL/make_mesh_3.h>
#include <CGAL/Image_3.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/bbox.h>
#include <CGAL/Side_of_triangle_mesh.h>
#endif

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>
#include <array>

// ============================================================
DBSMeshWorker::DBSMeshWorker(QObject* parent)
    : QObject(parent)
{}

// ============================================================
// 主流程
// ============================================================
void DBSMeshWorker::process()
{
    try {
        emit progressUpdated(0, "开始网格化...");

        // 检查输入
        if (!m_brainImage) {
            emit errorOccurred("未提供脑部影像数据");
            return;
        }

        // Step 1: 裁剪 ROI
        emit progressUpdated(5, "裁剪电极周围 ROI...");
        auto roiImage = extractROI();
        if (!roiImage) {
            emit errorOccurred("ROI 裁剪失败");
            return;
        }

        // Step 2: 构建合并标签图（仅脑组织 + 核团，无电极）
        emit progressUpdated(15, "构建合并标签图...");
        auto mergedLabel = buildMergedLabelMap(roiImage);
        if (!mergedLabel) {
            emit errorOccurred("标签图构建失败");
            return;
        }

        // Step 3: CGAL 网格化 — 根据 mode 分发
        bool meshOk = false;
        if (m_spec.meshingMode == dbs_fem::DBSSimSpec::CONFORMING) {
            emit progressUpdated(30, "CGAL Conforming 网格化 (路线 C)...");
            meshOk = runConformingMeshing(mergedLabel);
            if (!meshOk) {
                qWarning() << "[DBSMeshWorker] Conforming 网格化失败，回退到 image-based";
                emit progressUpdated(30, "回退: image-based 网格化...");
                meshOk = runCGALMeshing(mergedLabel);
            }
        } else {
            emit progressUpdated(30, "CGAL Image-based 网格化 (旧 pipeline)...");
            meshOk = runCGALMeshing(mergedLabel);
        }
        if (!meshOk) {
            emit errorOccurred("CGAL 网格化失败");
            return;
        }

        emit progressUpdated(100, "网格化完成");
        emit finished(m_outputMeshPath);

    } catch (const std::exception& e) {
        emit errorOccurred(QString("网格化异常: %1").arg(e.what()));
    } catch (...) {
        emit errorOccurred("网格化未知异常");
    }
}

// ============================================================
// Step 1: 以 target 为中心裁剪 ROI
// ============================================================
vtkSmartPointer<vtkImageData> DBSMeshWorker::extractROI()
{
    if (!m_brainImage) return nullptr;

    double origin[3], spacing[3];
    int extent[6];
    m_brainImage->GetOrigin(origin);
    m_brainImage->GetSpacing(spacing);
    m_brainImage->GetExtent(extent);

    double half = m_spec.roiHalfSize;  // mm

    // target 转换为体素索引范围
    int roiExt[6];
    for (int i = 0; i < 3; ++i) {
        double lo = m_spec.target[i] - half;
        double hi = m_spec.target[i] + half;
        roiExt[2*i]     = std::max(extent[2*i],   static_cast<int>(std::floor((lo - origin[i]) / spacing[i])));
        roiExt[2*i + 1] = std::min(extent[2*i+1], static_cast<int>(std::ceil((hi - origin[i]) / spacing[i])));
    }

    qDebug() << "[DBSMeshWorker] ROI extent:"
             << roiExt[0] << roiExt[1] << roiExt[2]
             << roiExt[3] << roiExt[4] << roiExt[5];

    vtkNew<vtkExtractVOI> extract;
    extract->SetInputData(m_brainImage);
    extract->SetVOI(roiExt);
    extract->Update();

    vtkSmartPointer<vtkImageData> roi = vtkSmartPointer<vtkImageData>::New();
    roi->DeepCopy(extract->GetOutput());

    // vtkExtractVOI 保留原始 extent 偏移，重置为 0 基索引并修正 origin
    int roiExtNow[6];
    roi->GetExtent(roiExtNow);
    double roiOrig[3], roiSpc[3];
    roi->GetOrigin(roiOrig);
    roi->GetSpacing(roiSpc);
    roi->SetOrigin(
        roiOrig[0] + roiExtNow[0] * roiSpc[0],
        roiOrig[1] + roiExtNow[2] * roiSpc[1],
        roiOrig[2] + roiExtNow[4] * roiSpc[2]);
    roi->SetExtent(
        0, roiExtNow[1] - roiExtNow[0],
        0, roiExtNow[3] - roiExtNow[2],
        0, roiExtNow[5] - roiExtNow[4]);

    int dims[3];
    double roiBounds[6];
    roi->GetDimensions(dims);
    roi->GetBounds(roiBounds);
    qDebug() << "[DBSMeshWorker] ROI dims:" << dims[0] << dims[1] << dims[2]
             << "spacing:" << roiSpc[0] << roiSpc[1] << roiSpc[2]
             << "bounds:" << roiBounds[0] << roiBounds[1] << roiBounds[2]
             << roiBounds[3] << roiBounds[4] << roiBounds[5];

    return roi;
}

// ============================================================
// Step 2: 构建合并标签图 —— 仅脑组织 + 核团（不含电极）
// ============================================================
vtkSmartPointer<vtkImageData> DBSMeshWorker::buildMergedLabelMap(vtkSmartPointer<vtkImageData> roiImage)
{
    if (!roiImage) return nullptr;

    int dims[3];
    double spacing[3], origin[3];
    roiImage->GetDimensions(dims);
    roiImage->GetSpacing(spacing);
    roiImage->GetOrigin(origin);

    // 创建空白标签图 (unsigned char)
    vtkSmartPointer<vtkImageData> labelMap = vtkSmartPointer<vtkImageData>::New();
    labelMap->SetDimensions(dims);
    labelMap->SetSpacing(spacing);
    labelMap->SetOrigin(origin);
    labelMap->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

    int totalVoxels = dims[0] * dims[1] * dims[2];
    unsigned char* labelPtr = static_cast<unsigned char*>(labelMap->GetScalarPointer());

    // 初始化: 非零体素 → label 10 (脑组织), 零体素 → 0 (背景)
    for (int idx = 0; idx < totalVoxels; ++idx) {
        double val = roiImage->GetScalarComponentAsDouble(
            idx % dims[0],
            (idx / dims[0]) % dims[1],
            idx / (dims[0] * dims[1]),
            0);
        labelPtr[idx] = (std::abs(val) > 1e-6)
                       ? static_cast<unsigned char>(dbs_fem::LABEL_BRAIN_TISSUE)
                       : 0;
    }

    // 叠加核团标签 (如果有)
    if (m_labelImage) {
        double lOrigin[3], lSpacing[3];
        int lExtent[6];
        m_labelImage->GetOrigin(lOrigin);
        m_labelImage->GetSpacing(lSpacing);
        m_labelImage->GetExtent(lExtent);

        for (int z = 0; z < dims[2]; ++z) {
            for (int y = 0; y < dims[1]; ++y) {
                for (int x = 0; x < dims[0]; ++x) {
                    double wx = origin[0] + x * spacing[0];
                    double wy = origin[1] + y * spacing[1];
                    double wz = origin[2] + z * spacing[2];

                    int lx = static_cast<int>(std::round((wx - lOrigin[0]) / lSpacing[0]));
                    int ly = static_cast<int>(std::round((wy - lOrigin[1]) / lSpacing[1]));
                    int lz = static_cast<int>(std::round((wz - lOrigin[2]) / lSpacing[2]));

                    if (lx >= lExtent[0] && lx <= lExtent[1] &&
                        ly >= lExtent[2] && ly <= lExtent[3] &&
                        lz >= lExtent[4] && lz <= lExtent[5])
                    {
                        double lVal = m_labelImage->GetScalarComponentAsDouble(lx, ly, lz, 0);
                        int nucLabel = static_cast<int>(std::round(lVal));
                        if (nucLabel >= 1 && nucLabel <= 6) {
                            labelPtr[z * dims[0] * dims[1] + y * dims[0] + x] =
                                static_cast<unsigned char>(nucLabel);
                        }
                    }
                }
            }
        }
    }

    // 【P2 方案 0】不再光栅化电极！电极标签将在 runCGALMeshing 中
    //  通过 STL 后处理为四面体打标。

    std::map<int, int> labelCounts;
    for (int idx = 0; idx < totalVoxels; ++idx) {
        labelCounts[static_cast<int>(labelPtr[idx])]++;
    }
    qDebug() << "[DBSMeshWorker] labelMap 体素计数 (仅脑组织+核团，无电极):";
    for (const auto& kv : labelCounts) {
        qDebug() << "[DBSMeshWorker]   label" << kv.first << ":" << kv.second;
    }

    return labelMap;
}

// ============================================================
// (已弃用) 光栅化函数：方案 0 不再调用
// ============================================================
void DBSMeshWorker::rasterizePolyToLabel(vtkSmartPointer<vtkImageData> /*labelMap*/,
                                         vtkSmartPointer<vtkPolyData> /*poly*/,
                                         int /*labelValue*/)
{
    // P2 方案 0 不再使用此函数，保留空实现仅为接口兼容。
    qDebug() << "[DBSMeshWorker] rasterizePolyToLabel 已弃用 (P2 方案 0)";
}

// ============================================================
// 辅助：把 STL polydata 准备成 vtkSelectEnclosedPoints 的输入
//      (确保是三角网格 + 法线一致 + 闭合)
// ============================================================
static vtkSmartPointer<vtkPolyData> preparePolyForEnclosedTest(
    vtkSmartPointer<vtkPolyData> input)
{
    if (!input) return nullptr;
    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputData(input);
    tri->Update();

    vtkNew<vtkPolyDataNormals> norm;
    norm->SetInputConnection(tri->GetOutputPort());
    norm->ConsistencyOn();
    norm->AutoOrientNormalsOn();
    norm->ComputeCellNormalsOn();
    norm->ComputePointNormalsOff();
    norm->Update();

    auto out = vtkSmartPointer<vtkPolyData>::New();
    out->DeepCopy(norm->GetOutput());
    return out;
}

// ============================================================
// 辅助：用 STL 对一组点做"在 / 外"批量测试
//      返回 std::vector<bool>，长度 = points->GetNumberOfPoints()
// ============================================================
static std::vector<bool> testPointsInsidePoly(
    vtkSmartPointer<vtkPoints> points,
    vtkSmartPointer<vtkPolyData> closedPoly)
{
    std::vector<bool> result(points->GetNumberOfPoints(), false);
    if (!closedPoly || closedPoly->GetNumberOfCells() == 0) return result;

    auto sample = vtkSmartPointer<vtkPolyData>::New();
    sample->SetPoints(points);

    vtkNew<vtkSelectEnclosedPoints> sel;
    sel->SetInputData(sample);
    sel->SetSurfaceData(closedPoly);
    sel->SetTolerance(1e-6);
    sel->CheckSurfaceOff();   // 我们已经准备好闭合曲面，跳过自检节省时间
    sel->Update();

    auto* output = sel->GetOutput();
    if (!output) return result;
    auto* arr = output->GetPointData()->GetArray("SelectedPoints");
    if (!arr) return result;
    for (vtkIdType i = 0; i < arr->GetNumberOfTuples(); ++i) {
        result[i] = (arr->GetTuple1(i) > 0.5);
    }
    return result;
}

// ============================================================
// Step 3: CGAL 网格化 + STL 后处理打标签 (P2 方案 0)
// ============================================================
bool DBSMeshWorker::runCGALMeshing(vtkSmartPointer<vtkImageData> labelMap)
{
#ifdef DBS_HAS_CGAL
    if (!labelMap) return false;

    QElapsedTimer timer;
    timer.start();

    // ----------------------------------------------------------
    // 1. CGAL 类型定义
    // ----------------------------------------------------------
    typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
    typedef CGAL::Labeled_mesh_domain_3<K> Mesh_domain;
    typedef CGAL::Mesh_triangulation_3<Mesh_domain>::type Tr;
    typedef CGAL::Mesh_complex_3_in_triangulation_3<Tr> C3T3;
    typedef CGAL::Mesh_criteria_3<Tr> Mesh_criteria;

    // ----------------------------------------------------------
    // 2. 标签图 → CGAL Image_3
    // ----------------------------------------------------------
    vtkNew<vtkImageCast> caster;
    caster->SetInputData(labelMap);
    caster->SetOutputScalarTypeToUnsignedChar();
    caster->Update();
    vtkImageData* ucharImg = caster->GetOutput();

    int dims[3];
    double spacing[3], origin[3];
    ucharImg->GetDimensions(dims);
    ucharImg->GetSpacing(spacing);
    ucharImg->GetOrigin(origin);

    size_t numPixels = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    _image* im = _createImage(dims[0], dims[1], dims[2],
                              1, spacing[0], spacing[1], spacing[2],
                              1, WK_FIXED, SGN_UNSIGNED);
    if (!im) {
        qWarning() << "[CGAL] 创建图像结构失败";
        return false;
    }
    memcpy(im->data, ucharImg->GetScalarPointer(), numPixels * sizeof(unsigned char));
    CGAL::Image_3 image(im);

    Mesh_domain domain = Mesh_domain::create_labeled_image_mesh_domain(
        image, CGAL::parameters::default_values());

    // ----------------------------------------------------------
    // 3. Mesh criteria — P2 方案 0+ : 自适应尺寸场
    //    
    //    电极不在标签图里，但我们仍需在电极周围加密网格。
    //    做法：定义一个 Sizing Field，根据点 p 到电极轨迹（线段）
    //    的距离返回 cell_size：
    //      * 距离 ≤ R_near (1mm): cell_size = sizeNear (0.3mm)  —— 触点贴身区域
    //      * R_near < 距离 ≤ R_far (5mm): 线性过渡 0.3 → 1.5mm   —— 缓冲带
    //      * 距离 > R_far (5mm): cell_size = sizeFar (1.5mm)    —— 远场
    //    
    //    这样总 tet 数预期 200-500K，电极周围每个触点内部 30+ tet。
    // ----------------------------------------------------------
    typedef typename Mesh_domain::Index Mesh_index;

    struct ElectrodeSizingField {
        double targetX, targetY, targetZ;
        double dirX, dirY, dirZ;          // 单位方向 entry → target 的反方向（沿电极轴）
        double leadHalfLength;            // 电极沿轴方向的"半长"，超出则按端点
        double sizeNear;
        double sizeFar;
        double rNear;
        double rFar;

        // CGAL 6.x 仿函数签名: (Point_3, dim, Index) -> FT
        typename K::FT operator()(const typename K::Point_3& p,
                                  int /*dim*/,
                                  const Mesh_index& /*idx*/) const
        {
            // 把 p 投影到电极轴线段上，求距离
            double px = CGAL::to_double(p.x());
            double py = CGAL::to_double(p.y());
            double pz = CGAL::to_double(p.z());

            // 向量 from target_ras
            double vx = px - targetX;
            double vy = py - targetY;
            double vz = pz - targetZ;

            // 投影系数 (沿电极轴，可正可负)
            double t = vx * dirX + vy * dirY + vz * dirZ;
            // 限制在电极轴段 [-leadHalfLength, +leadHalfLength]
            // （leadHalfLength 包含触点段长度 + 一点裕量）
            if (t < -leadHalfLength) t = -leadHalfLength;
            if (t >  leadHalfLength) t =  leadHalfLength;

            // 投影点
            double qx = targetX + t * dirX;
            double qy = targetY + t * dirY;
            double qz = targetZ + t * dirZ;

            // 点到投影点距离
            double dx = px - qx, dy = py - qy, dz = pz - qz;
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);

            if (dist <= rNear) return sizeNear;
            if (dist >= rFar)  return sizeFar;
            // 线性过渡
            double r = (dist - rNear) / (rFar - rNear);
            return sizeNear + r * (sizeFar - sizeNear);
        }
    };

    // 计算电极轴方向 (entry → target 的反向，即从 target 朝 entry 走的反方向)
    // 实际我们用的是从 target 沿电极向 entry 延伸的方向 (-dir(entry→target))
    double dirX = m_spec.target[0] - m_spec.entry[0];
    double dirY = m_spec.target[1] - m_spec.entry[1];
    double dirZ = m_spec.target[2] - m_spec.entry[2];
    double dirLen = std::sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ);
    if (dirLen < 1e-9) {
        dirX = 0; dirY = 0; dirZ = 1; dirLen = 1;
    }
    // 把 entry→target 反向：从 target 看，触点向 entry 排列（即 -dir）
    dirX = -dirX / dirLen;
    dirY = -dirY / dirLen;
    dirZ = -dirZ / dirLen;

    // 电极占据范围: 从触点 0 中心(target 沿 -dir 偏移 ~tipLength + 0.75mm)
    // 到触点 last 上沿(再 +5mm 缓冲)。简化为"沿轴 ±15mm"。
    ElectrodeSizingField sizingField;
    sizingField.targetX = m_spec.target[0];
    sizingField.targetY = m_spec.target[1];
    sizingField.targetZ = m_spec.target[2];
    sizingField.dirX = dirX;
    sizingField.dirY = dirY;
    sizingField.dirZ = dirZ;
    sizingField.leadHalfLength = 15.0;  // mm, 半长
    sizingField.sizeNear = 0.4;          // mm, 触点紧邻区
    sizingField.sizeFar  = m_spec.meshBrainSize;  // mm, 远场 (默认 1.5)
    sizingField.rNear = 1.0;             // mm, 紧邻半径
    sizingField.rFar  = 5.0;             // mm, 缓冲外沿

    qDebug() << "[CGAL] 自适应尺寸场:"
             << "近场" << sizingField.sizeNear << "mm @ <=" << sizingField.rNear << "mm,"
             << "远场" << sizingField.sizeFar  << "mm @ >="  << sizingField.rFar  << "mm";

    Mesh_criteria criteria(
        CGAL::parameters::facet_angle = 30,
        CGAL::parameters::facet_size = 1.0,
        CGAL::parameters::facet_distance = 0.3,
        CGAL::parameters::cell_radius_edge_ratio = 3,
        CGAL::parameters::cell_size = sizingField   // 关键：传仿函数而非标量
    );

    emit progressUpdated(40, "CGAL 正在生成网格...");

    C3T3 c3t3;
    try {
        c3t3 = CGAL::make_mesh_3<C3T3>(domain, criteria);
    } catch (const std::exception& e) {
        qWarning() << "[CGAL] 网格生成异常:" << e.what();
        return false;
    }

    qint64 cgalMs = timer.elapsed();
    qDebug() << "[CGAL] 网格生成完毕! 顶点:" << c3t3.triangulation().number_of_vertices()
             << " 单元:" << c3t3.number_of_cells_in_complex()
             << " 用时:" << cgalMs << "ms";

    // ----------------------------------------------------------
    // 4. 收集顶点 + 四面体（先在内存中组装，再做后处理打标签）
    // ----------------------------------------------------------
    emit progressUpdated(70, "收集顶点和四面体...");

    std::map<typename Tr::Vertex_handle, size_t> v2idx;
    std::vector<std::array<double, 3>> verts;
    verts.reserve(c3t3.triangulation().number_of_vertices());

    for (auto vit = c3t3.triangulation().finite_vertices_begin();
         vit != c3t3.triangulation().finite_vertices_end(); ++vit)
    {
        if (c3t3.in_dimension(vit) > -1) {
            v2idx[vit] = verts.size();
            verts.push_back({
                vit->point().x() + origin[0],
                vit->point().y() + origin[1],
                vit->point().z() + origin[2]
            });
        }
    }

    struct Tet { size_t v[4]; int label; };
    std::vector<Tet> tets;
    tets.reserve(c3t3.number_of_cells_in_complex());
    for (auto cit = c3t3.cells_in_complex_begin();
         cit != c3t3.cells_in_complex_end(); ++cit)
    {
        Tet t;
        for (int i = 0; i < 4; ++i) t.v[i] = v2idx[cit->vertex(i)];
        t.label = static_cast<int>(c3t3.subdomain_index(cit));
        tets.push_back(t);
    }

    // ----------------------------------------------------------
    // 5. STL 后处理打标签 —— 方案 0 的核心
    //    先生成电极 STL（绝缘体杆 + 4 触点），
    //    然后用 vtkSelectEnclosedPoints 测每个 tet 的中心：
    //      * 在某个触点内 → tet.label = 101+i
    //      * 在绝缘体内（且不在任何触点内） → tet.label = 50
    //      * 不在电极内 → 保持原 label（脑组织/核团）
    // ----------------------------------------------------------
    emit progressUpdated(80, "STL 后处理打标签...");

    DBSElectrodeSTL stlGen;
    stlGen.setSpec(m_spec);
    bool stlOk = stlGen.generate();
    if (!stlOk) {
        qWarning() << "[DBSMeshWorker] 电极 STL 生成失败 — 网格中无电极标签！";
    }

    qint64 t1 = timer.elapsed();
    if (stlOk) {
        // 5.1 准备 STL 用于 inside 测试
        auto insulPoly = preparePolyForEnclosedTest(stlGen.getInsulatorPoly());
        auto encapPoly = preparePolyForEnclosedTest(stlGen.getEncapsulationPoly());
        std::vector<vtkSmartPointer<vtkPolyData>> contactPolys(m_spec.numContacts);
        for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
            contactPolys[i] = preparePolyForEnclosedTest(stlGen.getContactPoly(i));
        }

        // 5.2 构造 tet 中心点集
        auto centers = vtkSmartPointer<vtkPoints>::New();
        centers->SetNumberOfPoints(static_cast<vtkIdType>(tets.size()));
        for (size_t ci = 0; ci < tets.size(); ++ci) {
            const auto& t = tets[ci];
            double cx = 0.0, cy = 0.0, cz = 0.0;
            for (int k = 0; k < 4; ++k) {
                cx += verts[t.v[k]][0];
                cy += verts[t.v[k]][1];
                cz += verts[t.v[k]][2];
            }
            centers->SetPoint(static_cast<vtkIdType>(ci),
                              cx * 0.25, cy * 0.25, cz * 0.25);
        }

        // 5.3 先测触点（优先级高），再测绝缘体
        std::vector<int> newLabel(tets.size(), -1);  // -1 表示不在电极内

        for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
            if (!contactPolys[i]) continue;
            auto inside = testPointsInsidePoly(centers, contactPolys[i]);
            int hit = 0;
            for (size_t ci = 0; ci < tets.size(); ++ci) {
                if (inside[ci] && newLabel[ci] == -1) {
                    newLabel[ci] = dbs_fem::LABEL_CONTACT_BASE + i;
                    hit++;
                }
            }
            qDebug() << "[STL Tag] Contact" << i
                     << "(label" << (dbs_fem::LABEL_CONTACT_BASE + i) << ") inside tets:" << hit;
        }

        if (insulPoly) {
            auto inside = testPointsInsidePoly(centers, insulPoly);
            int hit = 0;
            for (size_t ci = 0; ci < tets.size(); ++ci) {
                if (inside[ci] && newLabel[ci] == -1) {
                    newLabel[ci] = dbs_fem::LABEL_ELECTRODE_BODY;
                    hit++;
                }
            }
            qDebug() << "[STL Tag] Insulator (label"
                     << dbs_fem::LABEL_ELECTRODE_BODY << ") inside tets:" << hit;
        }
        if (encapPoly) {
            auto inside = testPointsInsidePoly(centers, encapPoly);
            int hit = 0;
            for (size_t ci = 0; ci < tets.size(); ++ci) {
                if (inside[ci] && newLabel[ci] == -1) {
                    newLabel[ci] = dbs_fem::LABEL_ENCAPSULATION;
                    hit++;
                }
            }
            qDebug() << "[STL Tag] Encapsulation (label"
                     << dbs_fem::LABEL_ENCAPSULATION << ") inside tets:" << hit;
        }

        // 5.4 应用新标签
        size_t total = 0, electrode = 0;
        for (size_t ci = 0; ci < tets.size(); ++ci) {
            if (newLabel[ci] != -1) {
                tets[ci].label = newLabel[ci];
                electrode++;
            }
            total++;
        }
        qint64 stlMs = timer.elapsed() - t1;
        qDebug() << "[STL Tag] 重打标签完成: " << electrode << "/" << total
                 << "tets 在电极内, 用时:" << stlMs << "ms";
    }

    // ----------------------------------------------------------
    // 6. 写 .mesh 文件
    //    - Vertices: 直接输出
    //    - Triangles: 来自 c3t3.facets_in_complex (脑组织/核团/外边界界面)
    //                + 我们额外生成的"电极/脑组织"界面三角形
    //    - Tetrahedra: 用更新后的 tets[].label
    //
    //    关键：方案 0 不再让网格穿过电极内部，但 CGAL 没意识到这点。
    //    我们用一种简单做法：保留所有 CGAL 输出的界面三角形（用 -1 标记
    //    外边界、其他取 max 标签），然后**额外扫描每条 tet 边**，发现
    //    "电极 tet 与非电极 tet 相邻"的面，标为相应 contact label
    //    （101-104）或 insulator label（50）。
    //    
    //    这样 FEM 求解器看到的是：
    //      - tri label=-1 → ROI 外边界 V=0
    //      - tri label=101..104 → 触点表面，按极性施加 Dirichlet BC
    //      - tri label=50 → 绝缘体表面（无 BC，自然 Neumann）
    //      - tri label=10/1..6 → 脑组织/核团内部界面（无 BC）
    // ----------------------------------------------------------
    emit progressUpdated(90, "构造电极界面三角形...");

    QFile file(m_outputMeshPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[CGAL] 无法创建输出文件:" << m_outputMeshPath;
        return false;
    }
    QTextStream out(&file);

    // 6.1 输出 Vertices
    out << "MeshVersionFormatted 1\n";
    out << "Dimension 3\n\n";
    out << "Vertices\n" << verts.size() << "\n";
    for (const auto& p : verts) {
        out << p[0] << " " << p[1] << " " << p[2] << " 0\n";
    }

    // 6.2 收集 c3t3 输出的界面三角形（基本是 ROI 外边界 + 核团/脑组织界面）
    struct Tri { size_t v[3]; int label; };
    std::vector<Tri> tris;
    tris.reserve(c3t3.number_of_facets_in_complex());

    std::map<int, size_t> triLabelCounts;
    for (auto fit = c3t3.facets_in_complex_begin();
         fit != c3t3.facets_in_complex_end(); ++fit)
    {
        auto cell = fit->first;
        int idx_in_cell = fit->second;
        auto patch = c3t3.surface_patch_index(*fit);
        int d1 = patch.first;
        int d2 = patch.second;
        int label = (d1 == 0 || d2 == 0) ? -1 : std::max(d1, d2);

        Tri t;
        int vi = 0;
        for (int i = 0; i < 4; ++i) {
            if (i == idx_in_cell) continue;
            t.v[vi++] = v2idx[cell->vertex(i)];
        }
        t.label = label;
        tris.push_back(t);
        triLabelCounts[label]++;
    }

    // 6.3 额外构造电极/非电极的界面三角形
    //     遍历每个 tet 的 4 个面，如果"自身是电极 + 邻接 tet 不是电极（或反之）"
    //     就生成一个界面三角形。
    //     
    //     为了找邻接关系，先用面 hash：以排序后的 3 顶点 ID 元组作为 key。
    auto isElectrodeLabel = [](int lab) {
        return lab == dbs_fem::LABEL_ELECTRODE_BODY ||
               (lab >= dbs_fem::LABEL_CONTACT_BASE &&
                lab < dbs_fem::LABEL_CONTACT_BASE + 4);
    };

    // tetra face 的 4 种取法 (排除一个顶点):
    //   face k 由除 vertex(k) 之外的 3 个顶点构成
    auto faceKey = [](size_t a, size_t b, size_t c) {
        std::array<size_t, 3> k = { a, b, c };
        std::sort(k.begin(), k.end());
        return std::make_tuple(k[0], k[1], k[2]);
    };

    // tet face index → vertex indices (相对 tet)
    static const int FACE_VERT[4][3] = {
        {1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}
    };

    std::map<std::tuple<size_t, size_t, size_t>, std::pair<int, int>> faceToTet;
    // value = pair(tetIndex, faceIndex)；如果某个 face key 出现两次说明它是内部面

    std::map<std::tuple<size_t, size_t, size_t>, int> faceLabelByPair;
    // 用于第二次遍历：根据两侧 tet label 决定界面 label

    // 第一遍：建立 face → tet 映射，统计每个面被多少个 tet 占用
    std::map<std::tuple<size_t, size_t, size_t>, std::vector<int>> faceToTetIdx;
    for (size_t ci = 0; ci < tets.size(); ++ci) {
        const auto& t = tets[ci];
        for (int f = 0; f < 4; ++f) {
            auto key = faceKey(t.v[FACE_VERT[f][0]], t.v[FACE_VERT[f][1]], t.v[FACE_VERT[f][2]]);
            faceToTetIdx[key].push_back(static_cast<int>(ci));
        }
    }

    // 第二遍：找电极/非电极界面（也覆盖电极外边界，如果电极伸出 ROI）
    int extraElectrodeFaces = 0;
    for (const auto& kv : faceToTetIdx) {
        const auto& tetList = kv.second;
        if (tetList.size() == 1) {
            // 单侧面：可能是 ROI 外边界或电极伸出
            int lab = tets[tetList[0]].label;
            if (isElectrodeLabel(lab)) {
                // 一个电极 tet 紧贴 ROI 边界 → 该面属于电极
                Tri tt;
                tt.v[0] = std::get<0>(kv.first);
                tt.v[1] = std::get<1>(kv.first);
                tt.v[2] = std::get<2>(kv.first);
                tt.label = lab;
                tris.push_back(tt);
                triLabelCounts[lab]++;
                extraElectrodeFaces++;
            }
            // 非电极的单侧面已被 c3t3 输出（label=-1），不重复
            continue;
        }
        if (tetList.size() == 2) {
            int la = tets[tetList[0]].label;
            int lb = tets[tetList[1]].label;
            bool a_e = isElectrodeLabel(la);
            bool b_e = isElectrodeLabel(lb);
            if (a_e == b_e) continue;  // 同类相邻无需新界面
            int electrodeLab = a_e ? la : lb;

            Tri tt;
            tt.v[0] = std::get<0>(kv.first);
            tt.v[1] = std::get<1>(kv.first);
            tt.v[2] = std::get<2>(kv.first);
            tt.label = electrodeLab;
            tris.push_back(tt);
            triLabelCounts[electrodeLab]++;
            extraElectrodeFaces++;
        }
    }

    qDebug() << "[STL Tag] 额外电极界面三角形:" << extraElectrodeFaces;

    // 6.4 输出 Triangles
    out << "\nTriangles\n" << tris.size() << "\n";
    for (const auto& t : tris) {
        out << (t.v[0] + 1) << " "
            << (t.v[1] + 1) << " "
            << (t.v[2] + 1) << " "
            << t.label << "\n";
    }

    qDebug() << "[CGAL+STL] triangle label 计数:";
    for (const auto& kv : triLabelCounts) {
        qDebug() << "[CGAL+STL]   label" << kv.first << ":" << kv.second;
    }

    // 6.5 输出 Tetrahedra (使用更新后的 label)
    out << "\nTetrahedra\n" << tets.size() << "\n";
    std::map<int, size_t> tetLabelCounts;
    for (const auto& t : tets) {
        out << (t.v[0] + 1) << " " << (t.v[1] + 1) << " "
            << (t.v[2] + 1) << " " << (t.v[3] + 1) << " "
            << t.label << "\n";
        tetLabelCounts[t.label]++;
    }
    qDebug() << "[CGAL+STL] tetra label 计数 (已经过 STL 后处理):";
    for (const auto& kv : tetLabelCounts) {
        qDebug() << "[CGAL+STL]   label" << kv.first << ":" << kv.second;
    }

    out << "\nEnd\n";
    file.close();

    qint64 totalMs = timer.elapsed();
    qDebug() << "[DBSMeshWorker] 总耗时:" << totalMs << "ms";

    emit progressUpdated(95, "网格文件写入完成");
    return true;

#else
    emit errorOccurred("CGAL 未安装，无法执行网格化。请安装 CGAL 后重新编译并定义 DBS_HAS_CGAL。");
    return false;
#endif
}


// ============================================================
// (路线 C) Step 4b: Conforming Meshing
//
//   核心思想：
//     - 把脑组织外壳从 labelMap 提取成闭合 STL
//     - 把电极 (绝缘体 + 4 触点) 也作为 STL
//     - 用 Polyhedral_complex_mesh_domain_3 把它们拼成一个网格域
//     - 电极内部声明为 subdomain 0 (外部) → CGAL 自动挖空
//     - 每个 polyhedron 表面 CGAL 自动加密节点
//     - 给电极/脑壳分别指定不同的 facet_size 控制远场不过密
//
//   阶段 A: 暂时不处理核团 (整个脑视作均匀)，只有脑壳 + 电极
//   阶段 B: 后续在 mesh 完成后用 STL 后处理给核团内的 tet 重打 label 1-6
// ============================================================

#ifdef DBS_HAS_CGAL

namespace {

// ---- Helper: 从标签图提取某个 label 的闭合等值面 polydata ----
// 步骤: padding 0 (避免开口) → threshold 提取 binary → marching cubes
//        → 取最大连通域 → fill holes → triangulate → 法线一致化
static vtkSmartPointer<vtkPolyData> extractLabelSurface(
    vtkSmartPointer<vtkImageData> labelMap, int targetLabel)
{
    if (!labelMap) return nullptr;

    // 1) padding：在外围加一圈 0，保证标签区与背景之间有完整边界
    vtkNew<vtkImageConstantPad> padder;
    padder->SetInputData(labelMap);
    int ext[6];
    labelMap->GetExtent(ext);
    int padded[6] = {
        ext[0] - 1, ext[1] + 1,
        ext[2] - 1, ext[3] + 1,
        ext[4] - 1, ext[5] + 1
    };
    padder->SetOutputWholeExtent(padded);
    padder->SetConstant(0);
    padder->Update();

    // 2) threshold: 仅保留目标 label
    vtkNew<vtkImageThreshold> thr;
    thr->SetInputConnection(padder->GetOutputPort());
    thr->ThresholdBetween(targetLabel, targetLabel);
    thr->SetInValue(targetLabel);
    thr->SetOutValue(0);
    thr->ReplaceInOn();
    thr->ReplaceOutOn();
    thr->SetOutputScalarTypeToUnsignedChar();
    thr->Update();

    // 3) discrete marching cubes
    vtkNew<vtkDiscreteMarchingCubes> dmc;
    dmc->SetInputConnection(thr->GetOutputPort());
    dmc->SetNumberOfContours(1);
    dmc->SetValue(0, targetLabel);
    dmc->ComputeNormalsOff();
    dmc->ComputeGradientsOff();
    dmc->Update();

    // 4) 最大连通域 (避免噪点)
    vtkNew<vtkConnectivityFilter> conn;
    conn->SetInputConnection(dmc->GetOutputPort());
    conn->SetExtractionModeToLargestRegion();
    conn->Update();

    // 5) 清理 (合并重复点)
    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputData(vtkPolyData::SafeDownCast(conn->GetOutput()));
    clean->Update();

    // 6) 补洞 (确保闭合)
    vtkNew<vtkFillHolesFilter> fill;
    fill->SetInputConnection(clean->GetOutputPort());
    fill->SetHoleSize(1e9);
    fill->Update();

    // 7) 三角化 + 法线一致化 (外法线方向)
    vtkNew<vtkPolyDataNormals> norm;
    norm->SetInputConnection(fill->GetOutputPort());
    norm->ConsistencyOn();
    norm->AutoOrientNormalsOn();
    norm->SplittingOff();
    norm->Update();

    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(norm->GetOutputPort());
    tri->Update();

    auto out = vtkSmartPointer<vtkPolyData>::New();
    out->DeepCopy(tri->GetOutput());
    return out;
}

// ---- Helper: vtkPolyData → CGAL Polyhedron ----
typedef CGAL::Exact_predicates_inexact_constructions_kernel  KK;
typedef typename CGAL::Mesh_polyhedron_3<KK>::type           ConfPolyhedron;

static bool vtkPolyToCgalPoly(vtkPolyData* in, ConfPolyhedron& out, const char* tag = "poly")
{
    if (!in || in->GetNumberOfPoints() == 0) {
        qWarning() << "[Conforming]" << tag << "vtkPolyData 为空";
        return false;
    }

    // 预处理: clean (合并 1e-5 内的重复点) + triangulate
    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputData(in);
    clean->SetTolerance(0.0);          // 绝对容差
    clean->SetAbsoluteTolerance(1e-5);
    clean->ToleranceIsAbsoluteOn();
    clean->ConvertLinesToPointsOff();
    clean->ConvertPolysToLinesOff();
    clean->ConvertStripsToPolysOff();
    clean->PointMergingOn();
    clean->Update();

    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(clean->GetOutputPort());
    tri->Update();

    auto cleaned = tri->GetOutput();
    if (!cleaned || cleaned->GetNumberOfPoints() == 0
                 || cleaned->GetNumberOfCells() == 0) {
        qWarning() << "[Conforming]" << tag << "clean+tri 后无数据";
        return false;
    }

    // 收集点
    std::vector<typename KK::Point_3> pts;
    pts.reserve(cleaned->GetNumberOfPoints());
    for (vtkIdType i = 0; i < cleaned->GetNumberOfPoints(); ++i) {
        double p[3];
        cleaned->GetPoint(i, p);
        pts.emplace_back(p[0], p[1], p[2]);
    }

    // 收集三角形
    std::vector<std::vector<std::size_t>> polys;
    polys.reserve(cleaned->GetNumberOfCells());
    auto* cells = cleaned->GetPolys();
    if (!cells) {
        qWarning() << "[Conforming]" << tag << "无 polys";
        return false;
    }
    cells->InitTraversal();
    vtkNew<vtkIdList> idList;
    while (cells->GetNextCell(idList)) {
        if (idList->GetNumberOfIds() == 3) {
            polys.push_back({
                static_cast<std::size_t>(idList->GetId(0)),
                static_cast<std::size_t>(idList->GetId(1)),
                static_cast<std::size_t>(idList->GetId(2))
            });
        }
    }
    if (polys.empty()) {
        qWarning() << "[Conforming]" << tag << "无三角形";
        return false;
    }

    namespace PMP = CGAL::Polygon_mesh_processing;

    // 修复 polygon soup (合并重复点 + 排除退化三角形)
    PMP::repair_polygon_soup(pts, polys);
    PMP::orient_polygon_soup(pts, polys);

    bool isMesh = PMP::is_polygon_soup_a_polygon_mesh(polys);
    qDebug() << "[Conforming]" << tag
             << "after repair: pts=" << pts.size()
             << " polys=" << polys.size()
             << " is_mesh=" << isMesh;
    if (!isMesh) {
        qWarning() << "[Conforming]" << tag << "polygon soup 不是有效 mesh";
        return false;
    }

    out.clear();
    PMP::polygon_soup_to_polygon_mesh(pts, polys, out);
    if (!CGAL::is_triangle_mesh(out)) PMP::triangulate_faces(out);

    bool valid    = out.is_valid();
    bool closed   = CGAL::is_closed(out);
    qDebug() << "[Conforming]" << tag
             << "polyhedron: faces=" << out.size_of_facets()
             << " vertices=" << out.size_of_vertices()
             << " valid=" << valid
             << " closed=" << closed;

    if (!valid) {
        qWarning() << "[Conforming]" << tag << "polyhedron not valid";
        return false;
    }
    if (!closed) {
        // 尝试缝合 border
        PMP::stitch_borders(out);
        closed = CGAL::is_closed(out);
        qDebug() << "[Conforming]" << tag << "after stitch closed=" << closed;
    }
    if (!closed) {
        qWarning() << "[Conforming]" << tag << "polyhedron 仍未闭合";
        return false;
    }

    // 修正法向 (outward): 测试 bbox 中心是否被识别为内部
    //   bbox 中心对凸 polyhedron 一定在 mesh 内部 (我们的 brain hull 近凸,
    //   electrode cylinders 严格凸)。所以:
    //     - sotm(center) == BOUNDED_SIDE → 法向朝外 OK
    //     - 否则 → 法向朝内，翻转
    //   这个测试在自身 mesh 局部进行，与场景其他 polyhedron 无关，
    //   比 orient_to_bound_a_volume / 远点法更可靠。
    auto bbox = PMP::bbox(out);
    typename KK::Point_3 bboxCenter(
        (bbox.xmin() + bbox.xmax()) * 0.5,
        (bbox.ymin() + bbox.ymax()) * 0.5,
        (bbox.zmin() + bbox.zmax()) * 0.5
    );
    CGAL::Side_of_triangle_mesh<ConfPolyhedron, KK> sotm(out);
    auto side = sotm(bboxCenter);
    if (side != CGAL::ON_BOUNDED_SIDE) {
        PMP::reverse_face_orientations(out);
        side = CGAL::Side_of_triangle_mesh<ConfPolyhedron, KK>(out)(bboxCenter);
        qDebug() << "[Conforming]" << tag
                 << "法向朝内 → 已翻转, 翻转后 side="
                 << (side == CGAL::ON_BOUNDED_SIDE ? "BOUNDED_OK" : "still WRONG");
    } else {
        qDebug() << "[Conforming]" << tag << "法向朝外 (bbox center BOUNDED) OK";
    }

    return true;
}

} // anonymous namespace

#endif // DBS_HAS_CGAL


bool DBSMeshWorker::runConformingMeshing(vtkSmartPointer<vtkImageData> labelMap)
{
#ifdef DBS_HAS_CGAL
    if (!labelMap) return false;

    QElapsedTimer timer;
    timer.start();

    typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
    typedef typename CGAL::Mesh_polyhedron_3<K>::type           Polyhedron;
    typedef CGAL::Polyhedral_complex_mesh_domain_3<K>           Mesh_domain;
    typedef typename CGAL::Mesh_triangulation_3<
        Mesh_domain, CGAL::Default, CGAL::Sequential_tag>::type Tr;
    typedef CGAL::Mesh_complex_3_in_triangulation_3<
        Tr, typename Mesh_domain::Corner_index,
        typename Mesh_domain::Curve_index>                       C3T3;
    typedef CGAL::Mesh_criteria_3<Tr>                            Mesh_criteria;
    typedef typename Mesh_domain::Subdomain_index                Subdomain_index;
    typedef typename Mesh_domain::Index                          Mesh_index;

    // ----------------------------------------------------------
    // Step C-1: 提取脑组织闭合 STL (labelMap 中 label 10 + 任意核团)
    //   阶段 A: 把所有非零 label 全部当作 "脑组织" (label 10)
    //           → 先把 labelMap 二值化再提一张外壳
    // ----------------------------------------------------------
    qDebug() << "[Conforming] Step C-1: 提取脑组织外壳 STL...";
    emit progressUpdated(35, "提取脑组织外壳 STL...");

    // 先把所有非零标签置为 10 (阶段 A 简化)
    vtkSmartPointer<vtkImageData> binBrain = vtkSmartPointer<vtkImageData>::New();
    binBrain->DeepCopy(labelMap);
    {
        int dims[3]; binBrain->GetDimensions(dims);
        size_t total = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
        unsigned char* p = static_cast<unsigned char*>(binBrain->GetScalarPointer());
        for (size_t i = 0; i < total; ++i) {
            if (p[i] != 0) p[i] = static_cast<unsigned char>(dbs_fem::LABEL_BRAIN_TISSUE);
        }
    }

    auto brainSurf = extractLabelSurface(binBrain, dbs_fem::LABEL_BRAIN_TISSUE);
    if (!brainSurf || brainSurf->GetNumberOfCells() == 0) {
        qWarning() << "[Conforming] 脑组织外壳提取失败";
        return false;
    }
    qDebug() << "[Conforming] 脑组织外壳:"
             << brainSurf->GetNumberOfPoints() << "pts,"
             << brainSurf->GetNumberOfCells() << "tri";

    // ----------------------------------------------------------
    // Step C-2: 生成电极 STL (绝缘体杆 + 4 触点)
    // ----------------------------------------------------------
    qDebug() << "[Conforming] Step C-2: 生成电极 STL...";
    emit progressUpdated(40, "生成电极 STL...");

    DBSElectrodeSTL stlGen;
    stlGen.setSpec(m_spec);
    if (!stlGen.generate()) {
        qWarning() << "[Conforming] 电极 STL 生成失败";
        return false;
    }
    auto insulSurf = stlGen.getInsulatorPoly();
    if (!insulSurf) return false;

    // ----------------------------------------------------------
    // Step C-3: vtkPolyData → CGAL Polyhedron (脑壳 + 包膜 + 绝缘体 + 4 触点)
    // ----------------------------------------------------------
    qDebug() << "[Conforming] Step C-3: 转换为 CGAL Polyhedron...";
    emit progressUpdated(45, "转换 CGAL Polyhedron...");

    Polyhedron polyBrain, polyEncap, polyInsul;
    Polyhedron polyContacts[4];
    if (!vtkPolyToCgalPoly(brainSurf, polyBrain, "brain")) {
        qWarning() << "[Conforming] 脑壳 polyhedron 无效 (非闭合或无效)";
        return false;
    }
    if (!vtkPolyToCgalPoly(insulSurf, polyInsul, "insulator")) {
        qWarning() << "[Conforming] 绝缘体 polyhedron 无效";
        return false;
    }
    bool hasEncap = m_spec.useEncapsulationLayer && m_spec.encapsulationThickness > 0.0;
    if (hasEncap) {
        auto encapSurf = stlGen.getEncapsulationPoly();
        if (!encapSurf || !vtkPolyToCgalPoly(encapSurf, polyEncap, "encapsulation")) {
            qWarning() << "[Conforming] 包膜 polyhedron 无效";
            return false;
        }
    }
    qDebug() << "[Conforming] polyBrain faces=" << polyBrain.size_of_facets()
             << " polyEncap faces=" << polyEncap.size_of_facets()
             << " polyInsul faces=" << polyInsul.size_of_facets();

    int contactCount = 0;
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        auto cp = stlGen.getContactPoly(i);
        if (!cp) continue;
        QByteArray tag = QString("contact%1").arg(i).toUtf8();
        if (!vtkPolyToCgalPoly(cp, polyContacts[i], tag.constData())) {
            qWarning() << "[Conforming] 触点" << i << "polyhedron 无效";
            continue;
        }
        contactCount++;
        qDebug() << "[Conforming] polyContacts[" << i << "] faces="
                 << polyContacts[i].size_of_facets();
    }

    // ----------------------------------------------------------
    // Step C-4: 构造 polyhedral complex domain
    //   CGAL 6.0.1 实测 subdomain pair 使用 (inside_label, outside_label)。
    //   嵌套关系：brain hull 内部为 10；包膜在 brain 内；绝缘体在包膜内；触点在绝缘体内。
    // ----------------------------------------------------------
    std::vector<Polyhedron> polyhedra;
    std::vector<std::pair<Subdomain_index, Subdomain_index>> indices;

    polyhedra.push_back(polyBrain);
    indices.emplace_back(dbs_fem::LABEL_BRAIN_TISSUE, 0);

    if (hasEncap) {
        polyhedra.push_back(polyEncap);
        indices.emplace_back(dbs_fem::LABEL_ENCAPSULATION,
                             dbs_fem::LABEL_BRAIN_TISSUE);
    }

    polyhedra.push_back(polyInsul);
    indices.emplace_back(dbs_fem::LABEL_ELECTRODE_BODY,
                         hasEncap ? dbs_fem::LABEL_ENCAPSULATION
                                  : dbs_fem::LABEL_BRAIN_TISSUE);

    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        if (polyContacts[i].size_of_facets() == 0) continue;
        polyhedra.push_back(polyContacts[i]);
        indices.emplace_back(dbs_fem::LABEL_CONTACT_BASE + i,
                             dbs_fem::LABEL_ELECTRODE_BODY);
    }

    qDebug() << "[Conforming] domain polyhedra count =" << polyhedra.size();
    qDebug() << "[Conforming] encapsulation enabled =" << hasEncap
             << "label=" << dbs_fem::LABEL_ENCAPSULATION
             << "thickness=" << m_spec.encapsulationThickness << "mm";
    emit progressUpdated(50, "构造 polyhedral complex domain...");

    Mesh_domain domain(polyhedra.begin(), polyhedra.end(),
                       indices.begin(),  indices.end());

    qDebug() << "[Conforming] detect_features(60) + detect_borders...";
    domain.detect_features(60.0);
    domain.detect_borders();

    // ----------------------------------------------------------
    // Step C-5: Mesh criteria
    //   facet_size 全局先用一个折中值，避免脑壳大量小三角形
    //   远场 cell_size 1.5 mm
    //   电极局部精度由 "电极是 polyhedron 边界" 自动保证
    //   (CGAL 6.0 的 sizing field 可按 patch 区分，但接口复杂；
    //    先用统一参数，验证 pipeline 后再迭代细化)
    // ----------------------------------------------------------
    struct ConformingElectrodeSizingField {
        double targetX, targetY, targetZ;
        double dirX, dirY, dirZ;
        double leadHalfLength;
        double sizeNear;
        double sizeFar;
        double rNear;
        double rFar;

        typename K::FT operator()(const typename K::Point_3& p,
                                  int,
                                  const Mesh_index&) const
        {
            double px = CGAL::to_double(p.x());
            double py = CGAL::to_double(p.y());
            double pz = CGAL::to_double(p.z());
            double vx = px - targetX;
            double vy = py - targetY;
            double vz = pz - targetZ;
            double t = vx * dirX + vy * dirY + vz * dirZ;
            if (t < -leadHalfLength) t = -leadHalfLength;
            if (t >  leadHalfLength) t =  leadHalfLength;
            double qx = targetX + t * dirX;
            double qy = targetY + t * dirY;
            double qz = targetZ + t * dirZ;
            double dx = px - qx;
            double dy = py - qy;
            double dz = pz - qz;
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist <= rNear) return sizeNear;
            if (dist >= rFar) return sizeFar;
            double a = (dist - rNear) / (rFar - rNear);
            return sizeNear + a * (sizeFar - sizeNear);
        }
    };

    double dirX = m_spec.target[0] - m_spec.entry[0];
    double dirY = m_spec.target[1] - m_spec.entry[1];
    double dirZ = m_spec.target[2] - m_spec.entry[2];
    double dirLen = std::sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ);
    if (dirLen < 1e-9) {
        dirX = 0.0; dirY = 0.0; dirZ = 1.0; dirLen = 1.0;
    }
    dirX = -dirX / dirLen;
    dirY = -dirY / dirLen;
    dirZ = -dirZ / dirLen;

    ConformingElectrodeSizingField cellSizingField;
    cellSizingField.targetX = m_spec.target[0];
    cellSizingField.targetY = m_spec.target[1];
    cellSizingField.targetZ = m_spec.target[2];
    cellSizingField.dirX = dirX;
    cellSizingField.dirY = dirY;
    cellSizingField.dirZ = dirZ;
    cellSizingField.leadHalfLength = 15.0;
    cellSizingField.sizeNear = (m_spec.meshContactSize > 0.0) ? m_spec.meshContactSize : 0.3;
    // 远场 cell_size 使用 m_spec.meshBrainSize（默认 1.5 mm）。
    // Phase F 曾临时试过 1.0 / 0.8 / 0.6 来排查远场衰减；
    // F1 修复验证表明主因是 surface label / BC，不是远场密度。
    cellSizingField.sizeFar = m_spec.meshBrainSize;
    cellSizingField.rNear = 1.5;
    cellSizingField.rFar = 5.0;

    ConformingElectrodeSizingField facetSizingField = cellSizingField;
    facetSizingField.sizeNear = 0.18;
    facetSizingField.sizeFar = 1.0;

    qDebug() << "[Conforming] electrode cell sizing field:"
             << "nearSize=" << cellSizingField.sizeNear
             << "nearR=" << cellSizingField.rNear
             << "farSize=" << cellSizingField.sizeFar
             << "farR=" << cellSizingField.rFar;
    qDebug() << "[Conforming] electrode facet sizing field:"
             << "nearSize=" << facetSizingField.sizeNear
             << "nearR=" << facetSizingField.rNear
             << "farSize=" << facetSizingField.sizeFar
             << "farR=" << facetSizingField.rFar;

    Mesh_criteria criteria(
        CGAL::parameters::edge_size(0.25),
        CGAL::parameters::facet_angle(25),
        CGAL::parameters::facet_size(facetSizingField),
        CGAL::parameters::facet_distance(0.1),   // 曲面逼近误差
        CGAL::parameters::cell_radius_edge_ratio(3),
        CGAL::parameters::cell_size(cellSizingField)
    );

    qDebug() << "[Conforming] make_mesh_3 ...";
    emit progressUpdated(55, "CGAL make_mesh_3 (耗时较长)...");

    C3T3 c3t3;
    try {
        c3t3 = CGAL::make_mesh_3<C3T3>(
            domain, criteria,
            CGAL::parameters::no_perturb(),
            CGAL::parameters::no_exude());
    } catch (const std::exception& e) {
        qWarning() << "[Conforming] make_mesh_3 异常:" << e.what();
        return false;
    }

    qint64 cgalMs = timer.elapsed();
    const std::size_t nv = c3t3.triangulation().number_of_vertices();
    const std::size_t nc = c3t3.number_of_cells_in_complex();
    const std::size_t nf = c3t3.number_of_facets_in_complex();
    qDebug() << "[Conforming] mesh built! vertices=" << nv
             << " tets=" << nc
             << " facets=" << nf
             << " 用时:" << cgalMs << "ms";

    if (nv == 0 || nc == 0) {
        qWarning() << "[Conforming] 生成的网格为空";
        return false;
    }

    // ----------------------------------------------------------
    // Step C-6: 收集 vertices / tetrahedra / triangles
    // ----------------------------------------------------------
    emit progressUpdated(80, "构造网格 .mesh 文件...");

    std::map<typename Tr::Vertex_handle, size_t> v2idx;
    std::vector<std::array<double, 3>> verts;
    verts.reserve(nv);
    for (auto vit = c3t3.triangulation().finite_vertices_begin();
         vit != c3t3.triangulation().finite_vertices_end(); ++vit)
    {
        if (c3t3.in_dimension(vit) > -1) {
            v2idx[vit] = verts.size();
            verts.push_back({
                vit->point().x(),
                vit->point().y(),
                vit->point().z()
            });
        }
    }

    struct Tet { size_t v[4]; int label; };
    std::vector<Tet> tets;
    tets.reserve(nc);
    std::map<int, size_t> tetLabelCounts;
    for (auto cit = c3t3.cells_in_complex_begin();
         cit != c3t3.cells_in_complex_end(); ++cit)
    {
        Tet t;
        for (int i = 0; i < 4; ++i) t.v[i] = v2idx[cit->vertex(i)];
        t.label = static_cast<int>(c3t3.subdomain_index(cit));
        tets.push_back(t);
        tetLabelCounts[t.label]++;
    }
    qDebug() << "[Conforming] tet label 计数:";
    for (auto const& kv : tetLabelCounts) {
        qDebug() << "[Conforming]   sub" << kv.first << ":" << kv.second;
    }

    // ----- Step C-7: 给表面三角形打标签 -----
    //   detect_features 会把每个 polyhedron 切成多个 patch，不能再依赖
    //   patch index。改用 facet 两侧 cell 的 subdomain 推断界面标签。
    qDebug() << "[Conforming] 给表面三角形打标签 (按 facet 两侧 subdomain)...";

    auto inferSurfaceLabel = [](int a, int b) -> int {
        // ============================================================
        // 表面三角面打标签：按 facet 两侧 tet 的 subdomain (label) 推断。
        // ------------------------------------------------------------
        // 历史教训：这段函数曾经是手动列举 (a, b) → label 的多个分支，
        // 凡是没列到的 pair 都 fallback 到 return -1，被求解器误当 ROI
        // 外边界接地为 V=0V。Phase F 4x VTA gap 主因就是漏写了
        // (encap, contact) 这一对。
        //
        // 现在改成"优先级规则"：每个 label 给一个固定优先级，共享面取
        // 优先级**高**的那一侧作为面 label。这样**任意 pair 一次性覆盖**，
        // 未来加新 label（核团 backfill / 灰白质细分 / fiber tract）只
        // 需要在 priority() 里加一行，不再需要在 if 里挨个枚举。
        //
        // 优先级设计原则（高 → 低）：
        //   1. contact (101..104)  — 电极金属表面，强 Dirichlet 锚点
        //   2. insulator (50)      — 电极绝缘体，物理屏障
        //   3. encap (51)          — 0.2mm 包膜薄壳
        //   4. nuclei (1..6)       — 核团（Phase G 才会出现的 label）
        //   5. brain tissue (10)   — 普通脑组织
        //   0. unknown / background — 触发 fallback warning
        //
        // 物理直觉：当某 cell pair 跨越层级时（比如 contact-encap、
        // encap-brain），界面应该归到"更结构化、更接近电极、BC 锚点
        // 更强"那一侧。这跟原版的几条手写规则结果完全一致：
        //   - (brain 10, encap 51)  → encap (51)             ✓ 跟原规则一致
        //   - (insul 50, encap 51)  → insulator (50)         ✓
        //   - (brain 10, insul 50)  → insulator (50)         ✓
        //   - (insul 50, contact)   → contact                ✓
        //   - (encap 51, contact)   → contact                ✓ Phase F 修复
        //   - (brain 10, contact)   → contact                ✓ no-encap case
        // 同时**自动覆盖**所有 nuclei 相关 pair（Phase G 启用后才会有）：
        //   - (nuclei 1..6, brain 10)    → nuclei            ★ 等 Phase G 触发
        //   - (nuclei 1..6, encap 51)    → encap             ★ 等 Phase G + 电极穿核团触发
        //   - (nuclei 1..6, insul 50)    → insulator         ★ 极少触发，但理论上也对
        //   - (nuclei 1..6, contact)     → contact           ★ 等 Phase G + no-encap + 穿核团
        //   - (nuclei 1..6, nuclei 1..6) → 选 a 或 b（同优先级）★ 不同核团接触
        //
        // 维护提醒：Phase G centroid backfill 启用后，FEM 日志里会第一
        // 次出现 (10, 1..6)、(51, 1..6) 等 surface label，**不应该**冒
        // 出 "Unknown surface pair" warning。如果冒了，说明这里的优先
        // 级规则还有遗漏。
        // ============================================================
        if (a > b) std::swap(a, b);

        // (0, X)：跟背景 (label 0) 接触一定是 ROI 外边界
        if (a == 0) return -1;

        auto priority = [](int lab) -> int {
            // 越靠近电极/BC 锚点优先级越高
            if (lab >= dbs_fem::LABEL_CONTACT_BASE &&
                lab <  dbs_fem::LABEL_CONTACT_BASE + 4)  return 5;  // contact 101..104
            if (lab == dbs_fem::LABEL_ELECTRODE_BODY)    return 4;  // insulator 50
            if (lab == dbs_fem::LABEL_ENCAPSULATION)     return 3;  // encap 51
            if (lab >= 1 && lab <= 6)                    return 2;  // nuclei 1..6
            if (lab == dbs_fem::LABEL_BRAIN_TISSUE)      return 1;  // brain 10
            return 0;  // unknown — 没 priority，触发 fallback
        };

        int pa = priority(a);
        int pb = priority(b);

        if (pa == 0 || pb == 0) {
            qWarning() << "[Conforming] Unknown surface pair ("
                       << a << "," << b
                       << "), labeled -1 (treated as outer boundary). "
                       << "If you just enabled Phase G nuclei backfill or added a new label, "
                       << "extend priority() in inferSurfaceLabel().";
            return -1;
        }

        // 共享面取优先级高的那一侧（同优先级时取 a，无所谓哪边）
        return (pa >= pb) ? a : b;
    };

    struct Tri { size_t v[3]; int label; };
    std::vector<Tri> tris;
    tris.reserve(nf);

    std::map<int, size_t> triLabelCounts;
    std::map<int, size_t> patchIndexCounts;

    for (auto fit = c3t3.facets_in_complex_begin();
         fit != c3t3.facets_in_complex_end(); ++fit)
    {
        auto cell = fit->first;
        int idx_in_cell = fit->second;
        Tri t;
        int vi = 0;
        for (int i = 0; i < 4; ++i) {
            if (i == idx_in_cell) continue;
            t.v[vi++] = v2idx[cell->vertex(i)];
        }
        int patch = static_cast<int>(c3t3.surface_patch_index(*fit));
        patchIndexCounts[patch]++;
        auto neigh = cell->neighbor(idx_in_cell);
        int sdA = c3t3.triangulation().is_infinite(cell)
                ? 0
                : static_cast<int>(c3t3.subdomain_index(cell));
        int sdB = c3t3.triangulation().is_infinite(neigh)
                ? 0
                : static_cast<int>(c3t3.subdomain_index(neigh));
        t.label = inferSurfaceLabel(sdA, sdB);
        tris.push_back(t);
        triLabelCounts[t.label]++;
    }

    qDebug() << "[Conforming] surface patch index 计数:";
    for (auto const& kv : patchIndexCounts) {
        qDebug() << "[Conforming]   patch" << kv.first << ":" << kv.second;
    }
    qDebug() << "[Conforming] triangle label 计数 (-1 = ROI/脑壳):";
    for (auto const& kv : triLabelCounts) {
        qDebug() << "[Conforming]   label" << kv.first << ":" << kv.second;
    }

    // ----------------------------------------------------------
    // Step C-7.5: Phase G nuclei centroid backfill
    //   This is intentionally after inferSurfaceLabel(), so nuclei
    //   labels affect only tetra material conductivity, not contact BC
    //   surface labels validated in Phase F.
    // ----------------------------------------------------------
    auto backfillNucleiLabels = [this](std::vector<Tet>& meshTets,
                                       const std::vector<std::array<double, 3>>& meshVerts) {
        if (!m_labelImage) {
            qWarning() << "[Phase G] nuclei backfill requested but label image is null; skipping";
            return;
        }

        double origin[3] = {0.0, 0.0, 0.0};
        double spacing[3] = {1.0, 1.0, 1.0};
        int extent[6] = {0, -1, 0, -1, 0, -1};
        m_labelImage->GetOrigin(origin);
        m_labelImage->GetSpacing(spacing);
        m_labelImage->GetExtent(extent);

        if (std::abs(spacing[0]) < 1e-12 ||
            std::abs(spacing[1]) < 1e-12 ||
            std::abs(spacing[2]) < 1e-12) {
            qWarning() << "[Phase G] label image spacing is invalid; skipping nuclei backfill";
            return;
        }

        std::array<size_t, 7> nucleiCounts = {{0, 0, 0, 0, 0, 0, 0}};
        size_t candidates = 0;
        size_t relabeled = 0;
        size_t outOfExtent = 0;
        size_t nonNucleiSamples = 0;

        for (auto& tet : meshTets) {
            if (tet.label != dbs_fem::LABEL_BRAIN_TISSUE) {
                continue;
            }
            candidates++;

            double c[3] = {0.0, 0.0, 0.0};
            for (int k = 0; k < 4; ++k) {
                const auto& p = meshVerts[tet.v[k]];
                c[0] += p[0];
                c[1] += p[1];
                c[2] += p[2];
            }
            c[0] *= 0.25;
            c[1] *= 0.25;
            c[2] *= 0.25;

            int idx[3] = {
                static_cast<int>(std::round((c[0] - origin[0]) / spacing[0])),
                static_cast<int>(std::round((c[1] - origin[1]) / spacing[1])),
                static_cast<int>(std::round((c[2] - origin[2]) / spacing[2]))
            };

            if (idx[0] < extent[0] || idx[0] > extent[1] ||
                idx[1] < extent[2] || idx[1] > extent[3] ||
                idx[2] < extent[4] || idx[2] > extent[5]) {
                outOfExtent++;
                continue;
            }

            double sample = m_labelImage->GetScalarComponentAsDouble(
                idx[0], idx[1], idx[2], 0);
            int nucLabel = static_cast<int>(std::round(sample));
            if (nucLabel >= 1 && nucLabel <= 6) {
                tet.label = nucLabel;
                nucleiCounts[static_cast<size_t>(nucLabel)]++;
                relabeled++;
            } else {
                nonNucleiSamples++;
            }
        }

        if (m_spec.logNucleiBackfill) {
            double ratio = candidates > 0
                ? static_cast<double>(relabeled) / static_cast<double>(candidates)
                : 0.0;
            qDebug() << "[Phase G] nuclei backfill:"
                     << "candidates(brain tet)=" << candidates
                     << "relabeled=" << relabeled
                     << "ratio=" << QString::number(ratio, 'f', 6)
                     << "outOfExtent=" << outOfExtent
                     << "nonNucleiSamples=" << nonNucleiSamples;
            for (int lab = 1; lab <= 6; ++lab) {
                qDebug() << "[Phase G]   label" << lab
                         << ":" << nucleiCounts[static_cast<size_t>(lab)] << "tets";
            }
        }

        std::map<int, size_t> finalLabelCounts;
        for (const auto& tet : meshTets) {
            finalLabelCounts[tet.label]++;
        }
        qDebug() << "[Phase G] tet label count after nuclei backfill:";
        for (auto const& kv : finalLabelCounts) {
            qDebug() << "[Phase G]   sub" << kv.first << ":" << kv.second;
        }
    };

    if (m_spec.useNucleiBackfill) {
        qDebug() << "[Phase G] nuclei centroid backfill enabled";
        backfillNucleiLabels(tets, verts);
    } else {
        qDebug() << "[Phase G] nuclei centroid backfill disabled (Phase F baseline mode)";
    }

    // ----------------------------------------------------------
    // Step C-8: 写 .mesh 文件
    // ----------------------------------------------------------
    QFile file(m_outputMeshPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[Conforming] 无法创建输出文件:" << m_outputMeshPath;
        return false;
    }
    QTextStream out(&file);
    out << "MeshVersionFormatted 1\n";
    out << "Dimension 3\n\n";

    out << "Vertices\n" << verts.size() << "\n";
    for (auto const& p : verts) {
        out << p[0] << " " << p[1] << " " << p[2] << " 0\n";
    }

    out << "\nTriangles\n" << tris.size() << "\n";
    for (auto const& t : tris) {
        out << (t.v[0] + 1) << " "
            << (t.v[1] + 1) << " "
            << (t.v[2] + 1) << " "
            << t.label << "\n";
    }

    out << "\nTetrahedra\n" << tets.size() << "\n";
    for (auto const& t : tets) {
        out << (t.v[0] + 1) << " " << (t.v[1] + 1) << " "
            << (t.v[2] + 1) << " " << (t.v[3] + 1) << " "
            << t.label << "\n";
    }

    out << "\nEnd\n";
    file.close();

    qint64 totalMs = timer.elapsed();
    qDebug() << "[Conforming] 总耗时:" << totalMs << "ms";

    emit progressUpdated(95, "网格文件写入完成");
    return true;

#else
    emit errorOccurred("CGAL 未启用，无法使用 conforming meshing");
    return false;
#endif
}
