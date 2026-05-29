#ifndef NNUNET_PREPROCESSOR_H
#define NNUNET_PREPROCESSOR_H
#pragma execution_character_set("utf-8")

// 必须先包含 torch，防止与 Qt/VTK 的 slots 关键字冲突
#undef slots
#include <torch/script.h>
#include <torch/torch.h>
//#include <torch/csrc/api/include/torch/torch.h>
#define slots Q_SLOTS

#include <string>
#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkNIFTIImageWriter.h>
#include <vector>
#include <array>

// 用于存储预处理结果和元数据（用于后续将预测结果还原回原始空间）
struct PreprocessedData {
    vtkSmartPointer<vtkImageData> image; // 处理后的图像数据
    int cropBounds[6];                   // [xmin, xmax, ymin, ymax, zmin, zmax]
    double originalSpacing[3];           // 原始 Spacing
    double originalOrigin[3];            // 必须保存原始原点
    int originalDims[3];                 // 原始尺寸
    double targetSpacing[3];             // 目标 Spacing
};

class NnUNetPreprocessor {
public:
    NnUNetPreprocessor();
    ~NnUNetPreprocessor();

    //根据 dataset_fingerprint.json 文件， nnUNet 的全局统计量
    const float NNUNET_MEAN = 16114.6328125f;
    const float NNUNET_STD = 1119.8135986328125f;

    // VTK 默认是 (x, y, z)，而 nnU-Net config通常是 (z, y, x)。
    // 这里我们假设输入参数顺序为 VTK 风格 (x, y, z)
    void setTargetSpacing(double x, double y, double z);
    bool savePreprocessedData(const PreprocessedData& data, const std::string& filename);

    // 执行完整的预处理流程
    PreprocessedData process(vtkSmartPointer<vtkImageData> inputImage);

    // 推理
    // 输入: 预处理后的数据, 模型路径, patch大小, 步长比例
    // 输出: 分割结果的 vtkImageData
    vtkSmartPointer<vtkImageData> inference(
        const PreprocessedData& inputData,
        const std::string& modelPath,
        std::vector<int> patchSize = { 40, 224, 192 }, // Z, Y, X
        std::vector<float> stepRatio = { 0.5, 0.5, 0.5 }
    );

    // 后处理：还原回原始空间
    vtkSmartPointer<vtkImageData> postProcess(
        vtkSmartPointer<vtkImageData> inferenceResult,
        const PreprocessedData& metadata
    );

private:
    double m_targetSpacing[3];

    // Step 1: 重采样
    vtkSmartPointer<vtkImageData> resampleImage(vtkSmartPointer<vtkImageData> input);

    // Step 2: 裁剪到前景
    vtkSmartPointer<vtkImageData> cropToForeground(vtkSmartPointer<vtkImageData> input, int outBounds[6]);

    // Step 3: Z-Score 归一化
    void normalizeImage(vtkSmartPointer<vtkImageData> input);

    // 内部推理辅助函数
    std::vector<int> computeSteps(int fullLen, int patchLen, float stepRatio);
};

#endif // NNUNET_PREPROCESSOR_H