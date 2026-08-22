//===================================================================
// ncdfanimate.cpp - 流场标量色图动态视觉生成引擎实现
// 基于专利：一种流场标量色图动态视觉生成方法
//          一种迭代图像重采样链的插值误差控制方法
//===================================================================

#include "ncdfanimate.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#ifndef PI
#define PI 3.14159265358979323846
#endif

//===================================================================
// 构造/析构
//===================================================================
NcdfAnimateEngine::NcdfAnimateEngine()
    : m_initialized(false)
    , m_width(0), m_height(0)
    , m_ni(0), m_nj(0)
    , m_frameIndex(0)
    , m_maxVelocity(0)
    , m_scalarMin(0), m_scalarMax(1)
{
}

NcdfAnimateEngine::~NcdfAnimateEngine()
{
}

//===================================================================
// 初始化
//===================================================================
bool NcdfAnimateEngine::initialize(double** uGrid, double** vGrid, double** scalarGrid,
                                    int ni, int nj, float scalarMin, float scalarMax,
                                    const AnimateConfig& config)
{
    if (!uGrid || !vGrid || !scalarGrid || ni < 2 || nj < 2)
        return false;
    
    m_config = config;
    m_ni = ni;
    m_nj = nj;
    // 动画尺寸与标量场网格一致
    m_width = ni;
    m_height = nj;
    m_scalarMin = scalarMin;
    m_scalarMax = scalarMax;
    m_frameIndex = 0;
    
    // 复制流场数据（展平为1D数组）
    m_uField.resize(ni * nj);
    m_vField.resize(ni * nj);
    m_scalarField.resize(ni * nj);
    
    m_maxVelocity = 0;
    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            int idx = j * ni + i;
            double u = uGrid[j][i];
            double v = vGrid[j][i];
            double s = scalarGrid[j][i];
            
            // 处理无效值
            if (!std::isfinite(u) || !std::isfinite(v)) {
                m_uField[idx] = 0;
                m_vField[idx] = 0;
            } else {
                m_uField[idx] = (float)u;
                m_vField[idx] = (float)v;
                float mag = std::sqrt(u*u + v*v);
                if (mag > m_maxVelocity) m_maxVelocity = mag;
            }
            
            if (!std::isfinite(s)) {
                m_scalarField[idx] = 0;  // 无效区域
            } else {
                m_scalarField[idx] = (float)s;
            }
        }
    }
    
    // 分配缓冲
    m_visualVelocity.resize(m_width * m_height);
    m_displacement = DisplacementMap(m_width, m_height);
    m_coordField1 = CoordField(m_width, m_height);
    m_coordField2 = CoordField(m_width, m_height);
    m_advectScalar1.resize(m_width * m_height);
    m_advectScalar2.resize(m_width * m_height);
    m_frameBuffer.resize(m_width * m_height * 4);
    
    // 预滤波标量场（一次）
    prefilterScalarField();
    
    // 计算视觉速度场
    computeVisualVelocityField();
    
    // 计算位移映射（稳态流场只需计算一次）
    computeDisplacementMap();
    
    // 初始化双坐标场为恒等映射
    m_coordField1.initIdentity();
    m_coordField2.initIdentity();
    
    // 第二坐标场预先迭代半个周期
    int halfPeriod = m_config.periodFrames / 2;
    for (int i = 0; i < halfPeriod; i++) {
        advectCoordField(m_coordField2);
    }
    
    // 构建颜色LUT
    buildColorLUT();
    
    m_initialized = true;
    return true;
}

//===================================================================
// 计算视觉速度场
// V_visual(x) = D * (|V(x)| / Vmax)^p * V(x) / |V(x)|
// 当|V(x)| <= threshold时，V_visual = 0
//===================================================================
void NcdfAnimateEngine::computeVisualVelocityField()
{
    float D = m_config.maxPixelDisplace;
    float p = m_config.powerExponent;
    float threshold = m_config.velocityThreshold;
    float invVmax = (m_maxVelocity > 0) ? (1.0f / m_maxVelocity) : 0;
    
    for (int j = 0; j < m_height; j++) {
        for (int i = 0; i < m_width; i++) {
            int idx = j * m_width + i;
            float u = m_uField[idx];
            float v = m_vField[idx];
            float mag = std::sqrt(u * u + v * v);
            
            if (mag <= threshold || m_maxVelocity <= 0) {
                m_visualVelocity[idx] = Vec2f(0, 0);
            } else {
                // 幂律映射后的速度大小
                float normalizedMag = mag * invVmax;
                float visualMag = D * std::pow(normalizedMag, p);
                // 保持方向
                float invMag = 1.0f / mag;
                m_visualVelocity[idx] = Vec2f(u * invMag * visualMag, v * invMag * visualMag);
            }
        }
    }
}

//===================================================================
// 四阶龙格-库塔法计算位移映射（逆向积分）
//===================================================================
void NcdfAnimateEngine::computeDisplacementMap()
{
    for (int j = 0; j < m_height; j++) {
        for (int i = 0; i < m_width; i++) {
            // 从输出像素位置(i,j)出发，逆向追踪一步
            float x = (float)i;
            float y = (float)y;
            
            // 采样视觉速度场（双线性插值采样）
            auto sampleVel = [&](float sx, float sy) -> Vec2f {
                sx = clampX(sx);
                sy = clampY(sy);
                int x0 = (int)std::floor(sx);
                int y0 = (int)std::floor(sy);
                int x1 = std::min(x0 + 1, m_width - 1);
                int y1 = std::min(y0 + 1, m_height - 1);
                float fx = sx - x0;
                float fy = sy - y0;
                
                Vec2f v00 = m_visualVelocity[y0 * m_width + x0];
                Vec2f v10 = m_visualVelocity[y0 * m_width + x1];
                Vec2f v01 = m_visualVelocity[y1 * m_width + x0];
                Vec2f v11 = m_visualVelocity[y1 * m_width + x1];
                
                Vec2f v;
                v.x = (1-fx)*(1-fy)*v00.x + fx*(1-fy)*v10.x + (1-fx)*fy*v01.x + fx*fy*v11.x;
                v.y = (1-fx)*(1-fy)*v00.y + fx*(1-fy)*v10.y + (1-fx)*fy*v01.y + fx*fy*v11.y;
                return v;
            };
            
            // RK4逆向积分（反方向）
            Vec2f k1 = sampleVel(x, y) * (-1.0f);
            Vec2f k2 = sampleVel(x + 0.5f*k1.x, y + 0.5f*k1.y) * (-1.0f);
            Vec2f k3 = sampleVel(x + 0.5f*k2.x, y + 0.5f*k2.y) * (-1.0f);
            Vec2f k4 = sampleVel(x + k3.x, y + k3.y) * (-1.0f);
            
            Vec2f displacement;
            displacement.x = (k1.x + 2*k2.x + 2*k3.x + k4.x) / 6.0f;
            displacement.y = (k1.y + 2*k2.y + 2*k3.y + k4.y) / 6.0f;
            
            m_displacement.at(i, j) = displacement;
        }
    }
}

//===================================================================
// 用位移映射更新坐标场（迭代复合）
// 新坐标场 = 旧坐标场 ∘ (I + displacement)
//===================================================================
void NcdfAnimateEngine::advectCoordField(CoordField& field)
{
    CoordField temp(field.width, field.height);
    
    for (int j = 0; j < field.height; j++) {
        for (int i = 0; i < field.width; i++) {
            // 当前坐标场的偏移
            Vec2f coordOffset = field.at(i, j);
            
            // 源采样位置 = 当前像素位置 + 坐标偏移
            float srcX = (float)i + coordOffset.x;
            float srcY = (float)j + coordOffset.y;
            
            // 从位移映射中采样（Keys三次卷积核插值）
            Vec2f disp = sampleCoordField(field, srcX, srcY);
            
            // 更新坐标偏移
            temp.at(i, j).x = coordOffset.x + disp.x;
            temp.at(i, j).y = coordOffset.y + disp.y;
        }
    }
    
    field = std::move(temp);
}

//===================================================================
// 从位移映射采样（双线性插值，简化版）
//===================================================================
Vec2f NcdfAnimateEngine::sampleCoordField(const CoordField& field, float x, float y) const
{
    // 实际应该用Keys卷积核，这里简化为双线性
    x = clampX(x);
    y = clampY(y);
    
    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);
    int x1 = std::min(x0 + 1, m_width - 1);
    int y1 = std::min(y0 + 1, m_height - 1);
    float fx = x - x0;
    float fy = y - y0;
    
    Vec2f v00 = m_displacement.at(x0, y0);
    Vec2f v10 = m_displacement.at(x1, y0);
    Vec2f v01 = m_displacement.at(x0, y1);
    Vec2f v11 = m_displacement.at(x1, y1);
    
    Vec2f result;
    result.x = (1-fx)*(1-fy)*v00.x + fx*(1-fy)*v10.x + (1-fx)*fy*v01.x + fx*fy*v11.x;
    result.y = (1-fx)*(1-fy)*v00.y + fx*(1-fy)*v10.y + (1-fx)*fy*v01.y + fx*fy*v11.y;
    return result;
}

//===================================================================
// Keys三次卷积核
//===================================================================
float NcdfAnimateEngine::keysKernel(float t, float a) const
{
    float at = std::abs(t);
    if (at < 1.0f) {
        return (a + 2) * at*at*at - (a + 3) * at*at + 1;
    } else if (at < 2.0f) {
        return a * at*at*at - 5*a * at*at + 8*a * at - 4*a;
    }
    return 0;
}

//===================================================================
// 预滤波标量场 -> 三次B样条系数场
//===================================================================
void NcdfAnimateEngine::prefilterScalarField()
{
    // 简化实现：直接复制标量场作为系数场
    // 完整实现应求解三次B样条系数
    m_scalarCoeffField = m_scalarField;
}

//===================================================================
// 用坐标场对标量场重采样（三次B样条插值）
//===================================================================
void NcdfAnimateEngine::resampleScalarField(const CoordField& coordField, std::vector<float>& output)
{
    for (int j = 0; j < m_height; j++) {
        for (int i = 0; i < m_width; i++) {
            Vec2f offset = coordField.at(i, j);
            float srcX = (float)i + offset.x;
            float srcY = (float)j + offset.y;
            
            // 采样标量系数场
            output[j * m_width + i] = sampleScalarField(srcX, srcY);
        }
    }
}

//===================================================================
// 三次B样条插值采样标量场
//===================================================================
float NcdfAnimateEngine::sampleScalarField(float x, float y) const
{
    x = clampX(x);
    y = clampY(y);
    
    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);
    int x1 = std::min(x0 + 1, m_width - 1);
    int y1 = std::min(y0 + 1, m_height - 1);
    float fx = x - x0;
    float fy = y - y0;
    
    // 双线性插值（简化，完整应为三次B样条）
    float v00 = m_scalarCoeffField[y0 * m_width + x0];
    float v10 = m_scalarCoeffField[y0 * m_width + x1];
    float v01 = m_scalarCoeffField[y1 * m_width + x0];
    float v11 = m_scalarCoeffField[y1 * m_width + x1];
    
    return (1-fx)*(1-fy)*v00 + fx*(1-fy)*v10 + (1-fx)*fy*v01 + fx*fy*v11;
}

//===================================================================
// 混合并着色生成帧
//===================================================================
void NcdfAnimateEngine::blendAndColorize(const std::vector<float>& advect1, 
                                           const std::vector<float>& advect2,
                                           float weight1, float weight2)
{
    // 归一化权重
    float totalWeight = weight1 + weight2;
    if (totalWeight > 0) {
        weight1 /= totalWeight;
        weight2 /= totalWeight;
    }
    
    float invRange = (m_scalarMax != m_scalarMin) ? 
                     1.0f / (m_scalarMax - m_scalarMin) : 1.0f;
    
    for (int j = 0; j < m_height; j++) {
        for (int i = 0; i < m_width; i++) {
            int idx = j * m_width + i;
            
            // 加权混合
            float mixed = weight1 * advect1[idx] + weight2 * advect2[idx];
            
            // 归一化到0~1
            float normalized = (mixed - m_scalarMin) * invRange;
            normalized = std::max(0.0f, std::min(1.0f, normalized));
            
            // 着色
            unsigned char r, g, b;
            valueToColor(normalized, r, g, b);
            
            int pixelIdx = idx * 4;
            m_frameBuffer[pixelIdx + 0] = r;
            m_frameBuffer[pixelIdx + 1] = g;
            m_frameBuffer[pixelIdx + 2] = b;
            m_frameBuffer[pixelIdx + 3] = 255;  // 不透明
        }
    }
}

//===================================================================
// 构建颜色查找表（海温色谱）
//===================================================================
void NcdfAnimateEngine::buildColorLUT()
{
    m_colorLUT.clear();
    // 海温色谱：紫->蓝->青->绿->黄->橙->红
    m_colorLUT.push_back({0.0f, 0x80, 0x00, 0xc0});  // 紫色
    m_colorLUT.push_back({0.1f, 0x40, 0x30, 0xff});  // 蓝色
    m_colorLUT.push_back({0.2f, 0x00, 0x90, 0xfa});  // 亮蓝
    m_colorLUT.push_back({0.3f, 0x00, 0xd8, 0xb0});  // 青色
    m_colorLUT.push_back({0.4f, 0x10, 0xbb, 0x20});  // 绿色
    m_colorLUT.push_back({0.5f, 0x90, 0xd0, 0x00});  // 黄绿
    m_colorLUT.push_back({0.6f, 0xf0, 0xd0, 0x00});  // 黄色
    m_colorLUT.push_back({0.7f, 0xf0, 0x70, 0x00});  // 橙色
    m_colorLUT.push_back({1.0f, 0xff, 0x00, 0x00});  // 红色
}

//===================================================================
// 数值->颜色转换
//===================================================================
void NcdfAnimateEngine::valueToColor(float val, unsigned char& r, unsigned char& g, unsigned char& b) const
{
    if (m_colorLUT.empty()) {
        r = g = b = 128;
        return;
    }
    
    val = std::max(0.0f, std::min(1.0f, val));
    
    // 找到对应的色段
    for (size_t i = 1; i < m_colorLUT.size(); i++) {
        if (val <= m_colorLUT[i].value) {
            const ColorStop& c0 = m_colorLUT[i-1];
            const ColorStop& c1 = m_colorLUT[i];
            float range = c1.value - c0.value;
            float t = (range > 0) ? (val - c0.value) / range : 0;
            // smoothstep
            t = t * t * (3.0f - 2.0f * t);
            r = (unsigned char)(c0.r + t * (c1.r - c0.r));
            g = (unsigned char)(c0.g + t * (c1.g - c0.g));
            b = (unsigned char)(c0.b + t * (c1.b - c0.b));
            return;
        }
    }
    
    // 超出范围，用最后一个颜色
    r = m_colorLUT.back().r;
    g = m_colorLUT.back().g;
    b = m_colorLUT.back().b;
}

//===================================================================
// 边界钳制
//===================================================================
float NcdfAnimateEngine::clampX(float x) const
{
    return std::max(0.0f, std::min(x, (float)(m_width - 1)));
}

float NcdfAnimateEngine::clampY(float y) const
{
    return std::max(0.0f, std::min(y, (float)(m_height - 1)));
}

//===================================================================
// 生成下一帧
//===================================================================
const unsigned char* NcdfAnimateEngine::generateNextFrame()
{
    if (!m_initialized) return nullptr;
    
    int T = m_config.periodFrames;
    int t = m_frameIndex % T;
    
    // 计算混合权重（正弦函数）
    // w1 = sin²(π*t/T), w2 = cos²(π*t/T)
    float phase = (float)t / (float)T * (float)PI;
    float w1 = std::sin(phase) * std::sin(phase);
    float w2 = std::cos(phase) * std::cos(phase);
    
    // 当权重为零时，重初始化对应坐标场
    if (w1 < 0.001f) {
        m_coordField1.initIdentity();
    }
    if (w2 < 0.001f) {
        m_coordField2.initIdentity();
    }
    
    // 用位移映射更新两个坐标场
    advectCoordField(m_coordField1);
    advectCoordField(m_coordField2);
    
    // 分别用两个坐标场重采样标量场
    resampleScalarField(m_coordField1, m_advectScalar1);
    resampleScalarField(m_coordField2, m_advectScalar2);
    
    // 混合并着色
    blendAndColorize(m_advectScalar1, m_advectScalar2, w1, w2);
    
    m_frameIndex++;
    return m_frameBuffer.data();
}

//===================================================================
// 重置动画
//===================================================================
void NcdfAnimateEngine::reset()
{
    m_frameIndex = 0;
    if (m_initialized) {
        m_coordField1.initIdentity();
        m_coordField2.initIdentity();
        // 第二坐标场预先迭代半个周期
        int halfPeriod = m_config.periodFrames / 2;
        for (int i = 0; i < halfPeriod; i++) {
            advectCoordField(m_coordField2);
        }
    }
}
