#include "NnUNetPreprocessor.h"

#include <vtkImageChangeInformation.h>
#include <vtkImageConstantPad.h>
#include <vtkImageReslice.h>
#include <vtkExtractVOI.h>
#include <vtkImageCast.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkMath.h>
#include <cmath>
#include <algorithm>
#include <QDebug> 

NnUNetPreprocessor::NnUNetPreprocessor() {
    // 默认目标 Spacing，对应你截图中的数值 (注意这里按 x, y, z 设置)
    // 截图 spacing = [2.0, 0.667, 0.667] (通常是 z, y, x)
    // VTK 中 spacing[0]是x, spacing[1]是y, spacing[2]是z
    // 所以这里设置为:
    m_targetSpacing[0] = 0.667;
    m_targetSpacing[1] = 0.667;
    m_targetSpacing[2] = 2.0;
}

NnUNetPreprocessor::~NnUNetPreprocessor() {}

void NnUNetPreprocessor::setTargetSpacing(double x, double y, double z) {
    m_targetSpacing[0] = x;
    m_targetSpacing[1] = y;
    m_targetSpacing[2] = z;
}

PreprocessedData NnUNetPreprocessor::process(vtkSmartPointer<vtkImageData> inputImage) {
    PreprocessedData result;

    // 0. 保存原始元数据
    inputImage->GetSpacing(result.originalSpacing);
    inputImage->GetOrigin(result.originalOrigin);   // 保存原点
    inputImage->GetDimensions(result.originalDims);
    std::copy(std::begin(m_targetSpacing), std::end(m_targetSpacing), std::begin(result.targetSpacing));

    // 1. 检查并重采样 (Step 1)
    vtkSmartPointer<vtkImageData> resampledImage = inputImage;
    double currentSpacing[3];
    inputImage->GetSpacing(currentSpacing);

    // 简单的浮点数比较容差
    bool needResample = (std::abs(currentSpacing[0] - m_targetSpacing[0]) > 1e-4) ||
        (std::abs(currentSpacing[1] - m_targetSpacing[1]) > 1e-4) ||
        (std::abs(currentSpacing[2] - m_targetSpacing[2]) > 1e-4);

    if (needResample) {
        qDebug() << "Step 1: Resampling from" << currentSpacing[0] << currentSpacing[1] << currentSpacing[2]
            << "to" << m_targetSpacing[0] << m_targetSpacing[1] << m_targetSpacing[2];
        resampledImage = resampleImage(inputImage);
    }
    else {
        qDebug() << "Step 1: Spacing match, skipping resample.";
    }

    // 2. 裁剪到前景 (Step 2)
    // 先将数据转为 float，因为归一化需要 float，且防止原始数据类型溢出
    vtkSmartPointer<vtkImageCast> castFilter = vtkSmartPointer<vtkImageCast>::New();
    castFilter->SetInputData(resampledImage);
    castFilter->SetOutputScalarTypeToFloat();
    castFilter->Update();
    vtkSmartPointer<vtkImageData> floatImage = castFilter->GetOutput();

    qDebug() << "Step 2: Cropping to foreground...";
    vtkSmartPointer<vtkImageData> croppedImage = cropToForeground(floatImage, result.cropBounds);

    // 3. 强度归一化 (Step 3)
    qDebug() << "Step 3: Z-Score Normalization...";
    normalizeImage(croppedImage);

    result.image = croppedImage;
    return result;
}

vtkSmartPointer<vtkImageData> NnUNetPreprocessor::resampleImage(vtkSmartPointer<vtkImageData> input) {
    int dims[3];
    double spacing[3];
    double origin[3];
    input->GetDimensions(dims);
    input->GetSpacing(spacing);
    input->GetOrigin(origin);

    // 计算新的尺寸
    int newDims[3];
    for (int i = 0; i < 3; i++) {
        newDims[i] = static_cast<int>(std::ceil(dims[i] * spacing[i] / m_targetSpacing[i]));
    }

    vtkSmartPointer<vtkImageReslice> reslice = vtkSmartPointer<vtkImageReslice>::New();
    reslice->SetInputData(input);
    reslice->SetOutputSpacing(m_targetSpacing);
    reslice->SetOutputOrigin(origin); // 保持原点一致
    reslice->SetOutputDimensionality(3);
    reslice->SetOutputExtent(0, newDims[0] - 1, 0, newDims[1] - 1, 0, newDims[2] - 1);

    // 对应截图中：图像使用三次插值 (order 3 -> Cubic)
    reslice->SetInterpolationModeToCubic();

    // 处理背景填充值，通常设为0（也就是空气/背景）
    reslice->SetBackgroundLevel(0);
    reslice->Update();

    return reslice->GetOutput();
}

vtkSmartPointer<vtkImageData> NnUNetPreprocessor::cropToForeground(vtkSmartPointer<vtkImageData> input, int outBounds[6]) {
    int dims[3];
    input->GetDimensions(dims);

    // 获取原始指针 (假设已经转为 float)
    float* ptr = static_cast<float*>(input->GetScalarPointer());

    int minX = dims[0], maxX = 0;
    int minY = dims[1], maxY = 0;
    int minZ = dims[2], maxZ = 0;
    bool foundForeground = false;

    // 遍历寻找 bounding box
    // 为了简化展示了三重循环，也可以考虑并行优化或步长优化
    for (int z = 0; z < dims[2]; z++) {
        for (int y = 0; y < dims[1]; y++) {
            for (int x = 0; x < dims[0]; x++) {
                // VTK 索引计算: z * (dims[1]*dims[0]) + y * dims[0] + x
                long long idx = static_cast<long long>(z) * dims[1] * dims[0] + y * dims[0] + x;
                if (std::abs(ptr[idx]) > 1e-6) { // 非零判断 (阈值防止 float 误差)
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                    if (z < minZ) minZ = z;
                    if (z > maxZ) maxZ = z;
                    foundForeground = true;
                }
            }
        }
    }

    if (!foundForeground) {
        // 如果全是黑的，返回原图
        //std::copy(std::begin(input->GetExtent()), std::end(input->GetExtent()), outBounds);
        int* extentPtr = input->GetExtent();
        std::copy(extentPtr, extentPtr + 6, outBounds);
        return input;
    }

    // 添加 Margin (截图中提到 2-4 voxel)
    int margin = 2;
    minX = std::max(0, minX - margin);
    maxX = std::min(dims[0] - 1, maxX + margin);
    minY = std::max(0, minY - margin);
    maxY = std::min(dims[1] - 1, maxY + margin);
    minZ = std::max(0, minZ - margin);
    maxZ = std::min(dims[2] - 1, maxZ + margin);

    // 记录 Bounding Box
    outBounds[0] = minX; outBounds[1] = maxX;
    outBounds[2] = minY; outBounds[3] = maxY;
    outBounds[4] = minZ; outBounds[5] = maxZ;

    // 执行裁剪
    vtkSmartPointer<vtkExtractVOI> cropFilter = vtkSmartPointer<vtkExtractVOI>::New();
    cropFilter->SetInputData(input);
    cropFilter->SetVOI(minX, maxX, minY, maxY, minZ, maxZ);
    cropFilter->Update();

    return cropFilter->GetOutput();
}

void NnUNetPreprocessor::normalizeImage(vtkSmartPointer<vtkImageData> input) {
    int dims[3];
    input->GetDimensions(dims);
    long long numPixels = static_cast<long long>(dims[0]) * dims[1] * dims[2];

    float* ptr = static_cast<float*>(input->GetScalarPointer());

    // 1. 计算前景的 Mean 和 Std
    double sum = 0.0;
    double sumSq = 0.0;
    long long count = 0;

    // 遍历统计前景 (mask != 0, 这里简单用 value != 0)
    for (long long i = 0; i < numPixels; i++) {
        float val = ptr[i];
        if (std::abs(val) > 1e-6) { // 视为前景
            sum += val;
            sumSq += val * val;
            count++;
        }
    }

    if (count == 0) return; // 避免除零
    double mean = sum / count;
    // 标准差公式: sqrt(E[x^2] - (E[x])^2)
    double variance = (sumSq / count) - (mean * mean);
    double stdDev = std::sqrt(variance);

    // 防止标准差过小
    stdDev = std::max(stdDev, 1e-8);
    qDebug() << "Normalization Stats - Mean:" << mean << "Std:" << stdDev;

    //1. 第一步或使用严格遵循 nnUNet 的推理流程，强度归一化步骤使用全局常量
    //使用 nnUNet 预计算的全局统计量
    //double mean = NNUNET_MEAN;
    //double stdDev = NNUNET_STD;

    ////// 防止标准差为0 (虽然 nnUNet 的 std 很大，但保险起见)
    //if (stdDev < 1e-8) stdDev = 1.0;
    //qDebug() << "Step 3: Global Z-Score Normalization using nnUNet stats - Mean:" << mean << "Std:" << stdDev;


    // 2. 对整张图应用 Z-Score (包括背景，背景会变成负值)
    for (long long i = 0; i < numPixels; i++) {
        ptr[i] = static_cast<float>((ptr[i] - mean) / stdDev);
    }
}

bool NnUNetPreprocessor::savePreprocessedData(const PreprocessedData& data, const std::string& filename){

    if (!data.image) return false;

    vtkSmartPointer<vtkNIFTIImageWriter> writer = vtkSmartPointer<vtkNIFTIImageWriter>::New();
    writer->SetFileName(filename.c_str());
    writer->SetInputData(data.image);

    // 这一步很重要：设置 NIfTI 的头信息
    // 因为经历了裁剪和重采样，Origin 和 Spacing 已经改变，
    // vtkImageData 自身携带了这些信息，Writer 会自动读取。
    // 但是，为了确保兼容性，可以显式设置一些参数（视情况而定，通常默认即可）

    // 关键：确保保存的是 float 类型 (归一化后的结果)
    // 之前代码里已经是 Float 了，这里 Writer 会自动处理

    try {
        writer->Write();
        qDebug() << "Saved preprocessed NIfTI to:" << filename.c_str();

        // 打印一下最终保存的信息，方便在 Python 侧核对
        int dims[3];
        data.image->GetDimensions(dims);
        qDebug() << "Saved Dims (x,y,z):" << dims[0] << dims[1] << dims[2];

        return true;
    }
    catch (...) {
        qDebug() << "Failed to write NIfTI file.";
        return false;
    }
}

// 对应 Python: compute_positions_1d
std::vector<int> NnUNetPreprocessor::computeSteps(int fullLen, int patchLen, float stepRatio) {
    std::vector<int> positions;
    if (fullLen <= patchLen) {
        positions.push_back(0);
        return positions;
    }

    int step = std::max(1, static_cast<int>(patchLen * stepRatio));
    int pos = 0;
    while (true) {
        positions.push_back(pos);
        if (pos + patchLen >= fullLen) break;
        pos += step;
        // 确保最后一个窗口贴着边界
        if (pos + patchLen > fullLen) {
            pos = fullLen - patchLen;
        }
    }
    // 去重 (处理 pos 调整后可能重复的情况)
    auto last = std::unique(positions.begin(), positions.end());
    positions.erase(last, positions.end());

    return positions;
}

vtkSmartPointer<vtkImageData> NnUNetPreprocessor::inference(
    const PreprocessedData& inputData,
    const std::string& modelPath,
    std::vector<int> patchSize,
    std::vector<float> stepRatio
) {
    // 0. 准备设备
    torch::Device device(torch::kCPU);
    if (torch::cuda::is_available()) {
        qDebug() << "CUDA is available! Using GPU for inference.";
        device = torch::Device(torch::kCUDA);
    }
    else {
        qDebug() << "CUDA not available. Using CPU.";
    }

    // 1. 加载模型 (TorchScript)
    torch::jit::script::Module module;
    try {
        module = torch::jit::load(modelPath);
        module.to(device);
        module.eval();
        qDebug() << "Model loaded successfully.";
    }
    catch (const c10::Error& e) {
        qDebug() << "Error loading model:" << e.what();
        return nullptr;
    }

    // 2. VTK 数据转 Tensor
    // VTK 内存布局: X 变化最快, 然后 Y, 然后 Z。
    // 也就是 (Z, Y, X) 的连续内存。可以直接 map 到 Tensor。
    vtkSmartPointer<vtkImageData> img = inputData.image;
    int dims[3];
    img->GetDimensions(dims); // VTK dims: [x, y, z]

    // Python 代码期望: (Z, Y, X)
    int fullX = dims[0];
    int fullY = dims[1];
    int fullZ = dims[2];

    // 获取裸指针 (float*)
    float* rawPtr = static_cast<float*>(img->GetScalarPointer());

    // 创建 Tensor (共享内存，不拷贝) -> shape [Z, Y, X]
    // 注意: VTK flatten 是 C-order，对应 shape 应该是 (dimZ, dimY, dimX)
    auto options = torch::TensorOptions().dtype(torch::kFloat32);
    torch::Tensor inputVolume = torch::from_blob(rawPtr, { fullZ, fullY, fullX }, options); // CPU Tensor

    // 3. 准备滑动窗口
    int patchZ = patchSize[0];
    int patchY = patchSize[1];
    int patchX = patchSize[2];

    std::vector<int> zPositions = computeSteps(fullZ, patchZ, stepRatio[0]);
    std::vector<int> yPositions = computeSteps(fullY, patchY, stepRatio[1]);
    std::vector<int> xPositions = computeSteps(fullX, patchX, stepRatio[2]);

    qDebug() << "Total patches:" << zPositions.size() * yPositions.size() * xPositions.size();

    // 4. 初始化概率图 Accumulators (在 CPU 上，显存可能不够放整图)
    // 此时还不知道类别数 C，需要跑第一次推理才知道，或者默认设好。
    // 先用 nullptr 占位
    torch::Tensor probMap;
    torch::Tensor countMap = torch::zeros({ fullZ, fullY, fullX }, torch::kFloat32);

    int numClasses = 0;
    int patchCount = 0;

    // 5. 滑动窗口循环
    // 禁用梯度计算，节省内存
    torch::NoGradGuard no_grad;

    for (int z0 : zPositions) {
        for (int y0 : yPositions) {
            for (int x0 : xPositions) {
                patchCount++;
                if (patchCount % 10 == 0) qDebug() << "Processing patch" << patchCount;

                // 5.1 提取 Patch

                int currPatchZ = std::min(patchZ, fullZ);
                int currPatchY = std::min(patchY, fullY);
                int currPatchX = std::min(patchX, fullX);

                torch::Tensor patch = inputVolume.slice(0, z0, z0 + currPatchZ)
                    .slice(1, y0, y0 + currPatchY)
                    .slice(2, x0, x0 + currPatchX);

                // 如果 patch 小于目标尺寸 (说明原图比 patch 还小)，需要 Pad
                if (patch.size(0) < patchZ || patch.size(1) < patchY || patch.size(2) < patchX) {
                    // 计算需要 Pad 多少: (padLeft, padRight, padTop, padBottom, padFront, padBack)
                    // PyTorch pad 顺序是从最后一个维度开始
                    int padX = patchX - patch.size(2);
                    int padY = patchY - patch.size(1);
                    int padZ = patchZ - patch.size(0);
                    // 这里的 pad 策略需要和训练一致，通常是 Constant 0 或者 Replicate
                    patch = torch::nn::functional::pad(patch,
                        torch::nn::functional::PadFuncOptions({ 0, padX, 0, padY, 0, padZ }).mode(torch::kConstant).value(0));
                }

                // 5.2 构造 Input: (1, 1, Z, Y, X)
                auto inputTensor = patch.unsqueeze(0).unsqueeze(0).to(device); // 移入 GPU

                // 5.3 Forward
                // model(input) -> 可能是 Tensor 也可能是 Tuple，根据 model_ts.pt 的导出方式
                auto output = module.forward({ inputTensor });
                torch::Tensor logits;

                if (output.isTensor()) {
                    logits = output.toTensor();
                }
                else if (output.isTuple()) {
                    logits = output.toTuple()->elements()[0].toTensor();
                }

                // 5.4 Softmax & Move to CPU
                // logits shape: (1, C, Z, Y, X)
                auto probs = torch::softmax(logits, 1).squeeze(0).to(torch::kCPU); // (C, Z, Y, X)

                // 初始化 probMap (只执行一次)
                if (numClasses == 0) {
                    numClasses = probs.size(0);
                    probMap = torch::zeros({ numClasses, fullZ, fullY, fullX }, torch::kFloat32);
                }

                // 5.5 累加 (Scatter back)
                // 只取有效区域 (如果之前做了 padding，这里要切掉)
                auto validProbs = probs.slice(1, 0, currPatchZ)
                    .slice(2, 0, currPatchY)
                    .slice(3, 0, currPatchX);

                // 将 validProbs 加回 probMap 的对应位置
                // probMap[:, z0:..., y0:..., x0:...] += validProbs
                using namespace torch::indexing;
                probMap.index_put_({ Slice(), Slice(z0, z0 + currPatchZ), Slice(y0, y0 + currPatchY), Slice(x0, x0 + currPatchX) },
                    probMap.index({ Slice(), Slice(z0, z0 + currPatchZ), Slice(y0, y0 + currPatchY), Slice(x0, x0 + currPatchX) }) + validProbs);

                // 累加计数图
                countMap.index_put_({ Slice(z0, z0 + currPatchZ), Slice(y0, y0 + currPatchY), Slice(x0, x0 + currPatchX) },
                    countMap.index({ Slice(z0, z0 + currPatchZ), Slice(y0, y0 + currPatchY), Slice(x0, x0 + currPatchX) }) + 1.0);
            }
        }
    }

    qDebug() << "Sliding window finished. Aggregating...";

    // 6. 归一化 & Argmax
    // countMap > 0 防止除零 (虽然逻辑上肯定>0)
    countMap.clamp_min_(1.0);

    // avg = prob / count
    // 注意 probMap 是 (C, Z, Y, X), countMap 是 (Z, Y, X)，会自动广播
    probMap /= countMap;

    // seg = argmax(dim=0) -> (Z, Y, X)
    auto segTensor = torch::argmax(probMap, 0).to(torch::kShort); // 转为 short

    // 7. 转回 VTK ImageData
    vtkSmartPointer<vtkImageData> outputImage = vtkSmartPointer<vtkImageData>::New();
    outputImage->SetDimensions(fullX, fullY, fullZ); // VTK: X, Y, Z
    outputImage->SetSpacing(inputData.image->GetSpacing()); // 保持预处理后的 Spacing
    outputImage->SetOrigin(inputData.image->GetOrigin());
    outputImage->AllocateScalars(VTK_SHORT, 1);

    // 拷贝数据
    // segTensor 依然是 (Z, Y, X) C-contiguous
    // VTK 也是 (Z, Y, X) C-contiguous (X fastest)
    // 直接 memcpy
    short* vtkPtr = static_cast<short*>(outputImage->GetScalarPointer());
    std::memcpy(vtkPtr, segTensor.data_ptr<short>(), fullZ * fullY * fullX * sizeof(short));

    qDebug() << "Inference Complete.";
    return outputImage;
}

vtkSmartPointer<vtkImageData> NnUNetPreprocessor::postProcess(
    vtkSmartPointer<vtkImageData> inferenceResult,
    const PreprocessedData& metadata
) {
    qDebug() << "Starting Post-processing (Restore to original geometry)...";

    // 1. 计算 "重采样后" 但 "裁剪前" 的全尺寸
    // 公式: new_dim = old_dim * old_spacing / new_spacing
    int resampledFullDims[3];
    for (int i = 0; i < 3; i++) {
        resampledFullDims[i] = static_cast<int>(
            std::ceil(metadata.originalDims[i] * metadata.originalSpacing[i] / metadata.targetSpacing[i])
            );
    }

    // 2. Un-Crop (把推理结果放回全图的正确位置)

    // 2.1 修改 Extent 起点

    vtkSmartPointer<vtkImageChangeInformation> changeInfo = vtkSmartPointer<vtkImageChangeInformation>::New();
    changeInfo->SetInputData(inferenceResult);
    // 设置新的 extent 起点为 cropBounds 的 min 值
    changeInfo->SetExtentTranslation(
        metadata.cropBounds[0],
        metadata.cropBounds[2],
        metadata.cropBounds[4]
    );
    changeInfo->Update();

    // 2.2 Pad (填充) 周围的 0
    // 将图像从 "局部位置" 扩展到 "重采样后的全尺寸 (0 到 resampledFullDims-1)"
    vtkSmartPointer<vtkImageConstantPad> padFilter = vtkSmartPointer<vtkImageConstantPad>::New();

    padFilter->SetInputData(changeInfo->GetOutput());

    // 设置输出范围为整个重采样后的图像大小
    padFilter->SetOutputWholeExtent(
        0, resampledFullDims[0] - 1,
        0, resampledFullDims[1] - 1,
        0, resampledFullDims[2] - 1
    );

    padFilter->SetConstant(0);
    padFilter->Update();

    vtkSmartPointer<vtkImageData> fullResampledImage = padFilter->GetOutput();

    // 3. Un-Resample (还原回原始 Spacing)
    vtkSmartPointer<vtkImageReslice> reslice = vtkSmartPointer<vtkImageReslice>::New();
    reslice->SetInputData(fullResampledImage);

    // 设置目标 Spacing 为原始 Spacing
    reslice->SetOutputSpacing(metadata.originalSpacing);


    reslice->SetOutputDimensionality(3);
    reslice->SetOutputExtent(0, metadata.originalDims[0] - 1, 0, metadata.originalDims[1] - 1, 0, metadata.originalDims[2] - 1);


    reslice->SetInterpolationModeToNearestNeighbor();

    reslice->Update();

    return reslice->GetOutput();
}