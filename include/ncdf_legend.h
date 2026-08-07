#ifndef NCDF_LEGEND_H
#define NCDF_LEGEND_H

struct ColorStop {
    double value;
    unsigned char r, g, b;
};

class ncdfLegend {
public:
    ncdfLegend();
    ~ncdfLegend();

    void SetData(const ColorStop* stops, int count, const char* unit, const char* title);
    void Draw(int screenW, int screenH);
    void Clear();

private:
    ColorStop* m_stops;
    int m_count;
    char m_unit[32];
    char m_title[64];
};

#endif // NCDF_LEGEND_H
