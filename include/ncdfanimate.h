//===================================================================
// ncdfanimate.h - 流场标量色图动态视觉生成引擎
// 基于专利：一种流场标量色图动态视觉生成方法
//          一种迭代图像重采样链的插值误差控制方法
//===================================================================

#ifndef NCDF_ANIMATE_H
#define NCDF_ANIMATE_H

#include <vector>
#include <cmath>

// 前向声明
class ncdfOverlayFactory;

//===================================================================
// 2D向量场/标量场基础类型
//===================================================================
struct Vec2f {
    float x, y;
    Vec2f() : x(0), y(0) {}
    Vec2f(float x_, float y_) : x(x_), y(y_) {}
    Vec2f operator+(const Vec2f& o) const { return {x+o.x, y+o.y}; }
    Vec2f operator*(float s) const { return {x*s, y*s}; }
    float length() const { return std::sqrt(x*x + y*y); }
};

//===================================================================
// 动画参数配置
//===================================================================
struct AnimateConfig {
    // 视觉速度场参数
    float powerExponent;      // 幂指数p，范围0.4~0.8，默认0.6
    float maxPixelDisplace;   // 单帧最大像素位移D，范围1~3，默认2.0
    float velocityThreshold;  // 流速阈值，低于此值视觉速度为0
    
    // 动画周期参数
    int   periodFrames;       // 预定周期T（帧数），默认30
    
    // 插值参数
    float keysACoeff;         // Keys三次卷积核参数a，默认-0.5
    
    AnimateConfig()
        : powerExponent(0.6f)
        , maxPixelDisplace(2.0f)
        , velocityThreshold(0.01f)
        , periodFrames(30)
        , keysACoeff(-0.5f)
    {}
};

//===================================================================
// 坐标场（2D位移场）
// 存储每个像素对应的源采样位置偏移
//===================================================================
struct CoordField {
    int width, height;
    std::vector<Vec2f> data;  // [y*width + x] = {dx, dy}
    
    CoordField() : width(0), height(0) {}
    CoordField(int w, int h) : width(w), height(h), data(w * h) {}
    
    // 初始化为恒等映射（位移为0）
    void initIdentity() {
        for (auto& v : data) { v.x = 0; v.y = 0; }
    }
    
    Vec2f& at(int x, int y) { return data[y * width + x]; }
    const Vec2f& at(int x, int y) const { return data[y * width + x]; }
    
    bool empty() const { return width == 0 || height == 0; }
};

//===================================================================
// 位移映射（由速度场逆向积分得到）
//===================================================================
struct DisplacementMap {
    int width, height;
    std::vector<Vec2f> data;  // [y*width + x] = {dx, dy} 单步位移
    
    DisplacementMap() : width(0), height(0) {}
    DisplacementMap(int w, int h) : width(w), height(h), data(w * h) {}
    
    Vec2f& at(int x, int y) { return data[y * width + x]; }
    const Vec2f& at(int x, int y) const { return data[y * width + x]; }
    
    bool empty() const { return width == 0 || height == 0; }
};

//===================================================================
// 动画引擎类
//===================================================================
class NcdfAnimateEngine {
public:
    NcdfAnimateEngine();
    ~NcdfAnimateEngine();
    
    // 初始化：传入流场数据和标量场
    // uGrid, vGrid: 流速分量网格 [nj][ni]
    // scalarGrid: 标量场网格 [nj][ni]（海温/海盐）
    // ni, nj: 网格尺寸
    // scalarMin, scalarMax: 标量范围（用于归一化）
    bool initialize(double** uGrid, double** vGrid, double** scalarGrid,
                    int ni, int nj, float scalarMin, float scalarMax,
                    const AnimateConfig& config = AnimateConfig());
    
    // 生成下一帧动画，返回RGBA像素数据
    // 返回的buffer大小为 width*height*4
    const unsigned char* generateNextFrame();
    
    // 获取当前帧序号
    int getCurrentFrame() const { return m_frameIndex; }
    
    // 获取动画尺寸
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
    // 重置动画
    void reset();
    
    // 是否已初始化
    bool isInitialized() const { return m_initialized; }

private:
    // 核心算法步骤
    void computeVisualVelocityField();     // 计算视觉速度场
    void computeDisplacementMap();         // RK4逆向积分计算位移映射
    void advectCoordField(CoordField& field);  // 用位移映射更新坐标场
    void resampleScalarField(const CoordField& coordField, std::vector<float>& output);  // 重采样标量场
    void blendAndColorize(const std::vector<float>& advect1, const std::vector<float>& advect2,
                          float weight1, float weight2);  // 混合并着色
    
    // 插值工具
    float sampleScalarField(float x, float y) const;  // 三次B样条插值采样
    Vec2f sampleCoordField(const CoordField& field, float x, float y) const;  // Keys卷积核采样坐标场
    
    // 预滤波：计算标量系数场
    void prefilterScalarField();
    
    // Keys三次卷积核
    float keysKernel(float t, float a) const;
    
    // 边界钳制
    float clampX(float x) const;
    float clampY(float y) const;

private:
    bool m_initialized;
    int m_width, m_height;      // 动画尺寸（与标量场相同）
    int m_ni, m_nj;             // 原始网格尺寸
    int m_frameIndex;           // 当前帧序号
    
    AnimateConfig m_config;
    
    // 流场数据（原始网格坐标，非像素坐标）
    std::vector<float> m_uField;   // u分量
    std::vector<float> m_vField;   // v分量
    float m_maxVelocity;           // 最大流速模长
    
    // 标量场
    std::vector<float> m_scalarField;      // 原始标量场
    std::vector<float> m_scalarCoeffField; // 预滤波后的系数场
    float m_scalarMin, m_scalarMax;        // 标量范围
    
    // 视觉速度场（像素坐标）
    std::vector<Vec2f> m_visualVelocity;
    
    // 位移映射
    DisplacementMap m_displacement;
    
    // 双坐标场
    CoordField m_coordField1;  // 第一坐标场
    CoordField m_coordField2;  // 第二坐标场
    
    // 平流标量缓冲
    std::vector<float> m_advectScalar1;
    std::vector<float> m_advectScalar2;
    
    // 输出帧缓冲（RGBA）
    std::vector<unsigned char> m_frameBuffer;
    
    // 颜色查找表
    struct ColorStop {
        float value;
        unsigned char r, g, b;
    };
    std::vector<ColorStop> m_colorLUT;
    
    void buildColorLUT();
    void valueToColor(float val, unsigned char& r, unsigned char& g, unsigned char& b) const;
};

#endif // NCDF_ANIMATE_H
