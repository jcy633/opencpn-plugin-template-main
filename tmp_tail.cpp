

//===================================================================
// RenderParticles - Flow-field particle animation (GRIB-style)
//===================================================================
void ncdfOverlayFactory::RenderParticles(PlugIn_ViewPort *vp, bool doUpdate)
{
    if (!gui || !gui->gridu || !gui->gridv) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni <= 1 || nj <= 1) return;
    double tlat = gui->myMessage.firstGridPointLat;
    double tlon = gui->myMessage.firstGridPointLong;
    double blat = gui->myMessage.lastGridPointLat;
    double blon = gui->myMessage.lastGridPointLong;
    if (tlat >= blat || tlon >= blon) return;

    const int max_duration = 50;
    const int run_count = 6;
    const int history_size = 4;
    double density = 0.5;
    int total_particles = (int)(density * ni * nj);
    if (total_particles > 60000) total_particles = 60000;

    if (doUpdate) {
        for (int i = 0; i < (int)m_Particles.size(); i++) {
            Particle &p = m_Particles[i];
            if (++p.m_Run < run_count) continue;
            p.m_Run = 0;
            if (p.m_Duration > max_duration) {
                m_Particles[i] = m_Particles.back();
                m_Particles.pop_back();
                i--;
                continue;
            }
            p.m_Duration++;
            float *pp = p.m_History[p.m_HistoryPos].m_Pos;
            if (++p.m_HistorySize > history_size) p.m_HistorySize = history_size;
            if (++p.m_HistoryPos >= history_size) p.m_HistoryPos = 0;
            Particle::ParticleNode &n = p.m_History[p.m_HistoryPos];
            float *pos = n.m_Pos;
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, pp[0], pp[1], true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, pp[0], pp[1], true);
            double vkn = 0, ang = 0;
            if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
                double mag = sqrt(vx*vx + vy*vy);
                ang = atan2(vy, -vx) * 180.0 / PI + 180.0;
                vkn = mag * 3.6 / 1.852;
            }
            if (p.m_Duration < max_duration - history_size && vkn > 0 && vkn < 100) {
                double d = vkn * run_count;
                ang += 180;
                float angr = (float)(ang / 180.0 * PI);
                float latr = pp[1] * (float)PI / 180.0f;
                float D = (float)(d / 3443.0);
                float sD = sinf(D), cD = cosf(D);
                float sy = sinf(latr), cy = cosf(latr);
                float sa = sinf(angr), ca = cosf(angr);
                pos[0] = pp[0] + asinf(sa * sD / cy) * 180.0f / (float)PI;
                pos[1] = asinf(sy * cD + cy * sD * ca) * 180.0f / (float)PI;
                wxPoint ps; GetCanvasPixLL(vp, &ps, pos[1], pos[0]);
                n.m_Screen[0] = ps.x; n.m_Screen[1] = ps.y;
                unsigned char rgba[4]; GetSeaCurrentGraphicColorRaw(mag, rgba);
                n.m_Color[0] = rgba[0]; n.m_Color[1] = rgba[1]; n.m_Color[2] = rgba[2];
            } else {
                pos[0] = -10000;
            }
        }
        int remove = ((int)m_Particles.size() - total_particles) / 16;
        for (int i = 0; i < remove; i++) m_Particles.pop_back();
        int new_count = (total_particles - (int)m_Particles.size()) / 64;
        for (int npi = 0; npi < new_count; npi++) {
            float p[2];
            for (int attempt = 0; attempt < 20; attempt++) {
                p[0] = tlon + (float)rand()/RAND_MAX * (blon - tlon);
                p[1] = tlat + (float)rand()/RAND_MAX * (blat - tlat);
                double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, p[0], p[1], true);
                double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, p[0], p[1], true);
                if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) break;
            }
            Particle np; memset(&np, 0, sizeof(Particle));
            np.m_Duration = rand() % (max_duration / 2);
            np.m_HistoryPos = 0; np.m_HistorySize = 1;
            np.m_Run = npi % run_count;
            np.m_History[0].m_Pos[0] = p[0]; np.m_History[0].m_Pos[1] = p[1];
            wxPoint ps; GetCanvasPixLL(vp, &ps, p[1], p[0]);
            np.m_History[0].m_Screen[0] = ps.x; np.m_History[0].m_Screen[1] = ps.y;
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, p[0], p[1], true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, p[0], p[1], true);
            double mag = (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF) ? sqrt(vx*vx + vy*vy) : 0;
            unsigned char rgba[4]; GetSeaCurrentGraphicColorRaw(mag, rgba);
            np.m_History[0].m_Color[0] = rgba[0]; np.m_History[0].m_Color[1] = rgba[1]; np.m_History[0].m_Color[2] = rgba[2];
            m_Particles.push_back(np);
        }
    }

    // Update screen positions
    for (size_t k = 0; k < m_Particles.size(); k++) {
        Particle &p = m_Particles[k];
        for (int h = 0; h < p.m_HistorySize; h++) {
            if (p.m_History[h].m_Pos[0] == -10000) continue;
            wxPoint sp; GetCanvasPixLL(vp, &sp, p.m_History[h].m_Pos[1], p.m_History[h].m_Pos[0]);
            p.m_History[h].m_Screen[0] = sp.x; p.m_History[h].m_Screen[1] = sp.y;
        }
    }

    // Draw - GRIB style: iterate history backwards with interpolation, batch GL
    if (!m_pdc) {
#ifdef ocpnUSE_GL
        int maxVerts = (int)m_Particles.size() * history_size * 2;
        float *va = new float[maxVerts * 2];
        unsigned char *ca = new unsigned char[maxVerts * 4];
        int cnt = 0;
        for (size_t k = 0; k < m_Particles.size(); k++) {
            Particle &p = m_Particles[k];
            unsigned char alpha = 250;
            int i = p.m_HistoryPos;
            bool hasPrev = false;
            float prevX = 0, prevY = 0;
            for (int step = 0; step < p.m_HistorySize; step++) {
                if (p.m_History[i].m_Pos[0] != -10000) {
                    float *sp = p.m_History[i].m_Screen;
                    unsigned char *ci = p.m_History[i].m_Color;
                    float d = (float)p.m_Run / run_count;
                    float curX = hasPrev ? (d * prevX + (1-d) * sp[0]) : sp[0];
                    float curY = hasPrev ? (d * prevY + (1-d) * sp[1]) : sp[1];
                    if (hasPrev) {
                        va[cnt*2] = prevX; va[cnt*2+1] = prevY;
                        ca[cnt*4] = ci[0]; ca[cnt*4+1] = ci[1]; ca[cnt*4+2] = ci[2]; ca[cnt*4+3] = alpha;
                        cnt++;
                        va[cnt*2] = curX; va[cnt*2+1] = curY;
                        ca[cnt*4] = ci[0]; ca[cnt*4+1] = ci[1]; ca[cnt*4+2] = ci[2]; ca[cnt*4+3] = alpha;
                        cnt++;
                    }
                    prevX = curX; prevY = curY;
                    hasPrev = true;
                }
                alpha -= 240 / history_size;
                if (alpha < 20) break;
                if (--i < 0) i = history_size - 1;
                if (i == p.m_HistoryPos) break;
            }
        }
        if (cnt > 0) {
            glEnable(GL_LINE_SMOOTH); glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(2);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, va);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, ca);
            glDrawArrays(GL_LINES, 0, cnt);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisable(GL_LINE_SMOOTH); glDisable(GL_BLEND);
        }
        delete[] va;
        delete[] ca;
#endif
    }
}

OVERLAP Intersect(PlugIn_ViewPort *vp, double lat_min, double lat_max, double lon_min, double lon_max, double Marge)
{
    if (((vp->lon_min - Marge) > (lon_max + Marge)) || ((vp->lon_max + Marge) < (lon_min - Marge)) ||
        ((vp->lat_max + Marge) < (lat_min - Marge)) || ((vp->lat_min - Marge) > (lat_max + Marge)))
        return _OUT;
    if ((vp->lon_min <= lon_min) && (vp->lon_max >= lon_max) && (vp->lat_max >= lat_max) && (vp->lat_min <= lat_min))
        return _IN;
    return _ON;
}
