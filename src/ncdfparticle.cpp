//===================================================================
// Auto-split from ncdfoverlayfactory.cpp
//===================================================================

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"

#ifndef PI
#define PI 3.14159265
#endif

//===================================================================
// Particle system (GRIB port)
//===================================================================
#define MAX_PARTICLE_HISTORY 8

struct Particle {
    int m_Duration;
    int m_HistoryPos, m_HistorySize, m_Run;
    double m_Velocity;  // Track velocity for trail length control
    struct ParticleNode {
        float m_Pos[2];
        float m_Screen[2];
        unsigned char m_Color[3];
    } m_History[MAX_PARTICLE_HISTORY];
};

struct ParticleMap {
    ParticleMap() : history_size(0) { last_viewport.bValid = false; }
    ~ParticleMap() {}
    std::vector<Particle> m_Particles;
    int history_size;
    PlugIn_ViewPort last_viewport;
};

void ncdfOverlayFactory::ClearParticles() {
    delete static_cast<ParticleMap*>(m_ParticleMap);
    m_ParticleMap = nullptr;
}

void ncdfOverlayFactory::RenderParticles(PlugIn_ViewPort *vp)
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

    if (!m_ParticleMap) m_ParticleMap = new ParticleMap();
    ParticleMap *pm = static_cast<ParticleMap*>(m_ParticleMap);
    std::vector<Particle> &particles = pm->m_Particles;

    const int max_duration = 50;
    const int run_count = 6;
    int sliderVal = 5;
    if (gui && gui->pPlugIn) sliderVal = gui->pPlugIn->m_iParticleDensity;
    // GRIB formula: history_size = 27 / sqrt(density)
    // density = 0.2 * slider → slider=5 → density=1.0 → history_size=27 (capped at 8)
    double density = 0.2 * sliderVal;
    int history_size = 27 / sqrt(wxMax(density, 0.1));
    history_size = wxMax(4, wxMin(history_size, MAX_PARTICLE_HISTORY));

    // Viewport caching
    PlugIn_ViewPort &lvp = pm->last_viewport;
    if (lvp.bValid == false || vp->view_scale_ppm != lvp.view_scale_ppm ||
        vp->skew != lvp.skew || vp->rotation != lvp.rotation ||
        vp->clat != lvp.clat || vp->clon != lvp.clon) {
        for (auto it = particles.begin(); it != particles.end(); it++)
            for (int i = 0; i < it->m_HistorySize; i++) {
                Particle::ParticleNode &n = it->m_History[i];
                if (n.m_Pos[0] == -10000) continue;
                wxPoint ps;
                GetCanvasPixLL(vp, &ps, n.m_Pos[1], n.m_Pos[0]);
                n.m_Screen[0] = ps.x; n.m_Screen[1] = ps.y;
            }
        lvp = *vp;
    }

    // Update particle positions (timer-driven or burst after data change)
    bool shouldUpdate = m_bUpdateParticles || (m_particleBurstCount > 0);
    if (m_particleBurstCount > 0) m_particleBurstCount--;
    m_bUpdateParticles = false;
    if (shouldUpdate) {
    for (unsigned int i = 0; i < particles.size(); i++) {
        Particle &it = particles[i];
        if (++it.m_Run < run_count) continue;
        it.m_Run = 0;
        if (it.m_Duration > max_duration) {
            it = particles[particles.size() - 1];
            particles.pop_back();
            i--;
            continue;
        }
        it.m_Duration++;
        float *pp = it.m_History[it.m_HistoryPos].m_Pos;
        if (++it.m_HistorySize > history_size) it.m_HistorySize = history_size;
        if (++it.m_HistoryPos >= history_size) it.m_HistoryPos = 0;
        Particle::ParticleNode &n = it.m_History[it.m_HistoryPos];
        float(&p)[2] = n.m_Pos;
        double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, pp[0], pp[1], true);
        double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, pp[0], pp[1], true);
        double vkn = 0, ang;
        if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
            double mag = sqrt(vx * vx + vy * vy);
            ang = atan2(vx, vy) * 180.0 / PI;  // atan2(east, north) = bearing
            vkn = mag;
        } else { vkn = 0; ang = 0; }
        if (it.m_Duration < max_duration - history_size && vkn > 0.3 && vkn < 100) {
    // GRIB optimization: convert m/s to knots (1 m/s = 1.94 knots)
    // This makes particles move at the same speed as GRIB
    double d = vkn * run_count * 1.94;  // Match GRIB's knot-based speed
            float angr = (float)(ang / 180.0 * PI);
            float latr = pp[1] * (float)PI / 180.0f;
            float D = (float)(d / 3443.0);
            float sD = sinf(D), cD = cosf(D);
            float sy = sinf(latr), cy = cosf(latr);
            float sa = sinf(angr), ca = cosf(angr);
            p[0] = pp[0] + asinf(sa * sD / cy) * 180.0f / (float)PI;
            p[1] = asinf(sy * cD + cy * sD * ca) * 180.0f / (float)PI;

            // Viewport recycling: respawn off-screen particles within viewport
            wxPoint ps;
            GetCanvasPixLL(vp, &ps, p[1], p[0]);
            n.m_Screen[0] = ps.x; n.m_Screen[1] = ps.y;
            int margin = 50;
            if (ps.x < -margin || ps.x > (int)vp->pix_width + margin ||
                ps.y < -margin || ps.y > (int)vp->pix_height + margin) {
                // Respawn at random viewport pixel
                wxPoint rp(rand() % wxMax(1, (int)vp->pix_width),
                           rand() % wxMax(1, (int)vp->pix_height));
                double rlat, rlon;
                GetCanvasLLPix(vp, rp, &rlat, &rlon);
                p[0] = (float)rlon; p[1] = (float)rlat;
                n.m_Screen[0] = (float)rp.x; n.m_Screen[1] = (float)rp.y;
                // Clear old history
                for (int h = 0; h < MAX_PARTICLE_HISTORY; h++)
                    it.m_History[h].m_Pos[0] = -10000;
                it.m_HistorySize = 1;
                it.m_HistoryPos = 0;
                n.m_Pos[0] = p[0]; n.m_Pos[1] = p[1];
            }
            wxColour c = GetSeaCurrentGraphicColor(vkn);
            n.m_Color[0] = c.Red(); n.m_Color[1] = c.Green(); n.m_Color[2] = c.Blue();
            it.m_Velocity = vkn;  // Store velocity for trail length control
        } else { p[0] = -10000; }
    }

    // GRIB formula: total_particles = density * Ni * Nj
    int total_particles = (int)(density * ni * nj);
    int max_grid = ni * nj;
    if (total_particles > max_grid) total_particles = max_grid;
    if (total_particles > 100000) total_particles = 100000;
    int remove = ((int)particles.size() - total_particles) / 16;
    for (int i = 0; i < remove; i++) particles.pop_back();
    int run = 0;
    int new_count = (total_particles - (int)particles.size()) / 64;  // GRIB spawn rate
    int vpw = wxMax(1, (int)vp->pix_width);
    int vph = wxMax(1, (int)vp->pix_height);
    for (int npi = 0; npi < new_count; npi++) {
        float p[2];
        double vkn = 0, ang = 0;
        // Velocity-weighted spawning within viewport pixels
        for (int attempt = 0; attempt < 30; attempt++) {
            wxPoint rp(rand() % vpw, rand() % vph);
            double rlat, rlon;
            GetCanvasLLPix(vp, rp, &rlat, &rlon);
            p[0] = (float)rlon; p[1] = (float)rlat;
            double vx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, p[0], p[1], true);
            double vy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, p[0], p[1], true);
            if (vx != ncdf_NOTDEF && vy != ncdf_NOTDEF && isfinite(vx) && isfinite(vy)) {
                double mag = sqrt(vx * vx + vy * vy);
                if (mag < 0.3) continue;  // Skip below 0.3 m/s
                // Linear: prob = (mag - 0.3) / 1.7 — increases every 0.1 m/s
                double prob = wxMin(1.0, (mag - 0.3) / 1.7);
                if ((double)rand() / RAND_MAX < prob) {
                    vkn = mag; ang = atan2(vx, vy) * 180.0 / PI;
                    break;
                }
            }
        }
        if (vkn < 0.3) continue;  // No valid position found
        Particle np;
        np.m_Duration = rand() % (max_duration / 2);
        np.m_HistoryPos = 0; np.m_HistorySize = 1;
        np.m_Run = run++;
        if (run == run_count) run = 0;
        memcpy(np.m_History[0].m_Pos, p, sizeof p);
        wxPoint ps;
        GetCanvasPixLL(vp, &ps, p[1], p[0]);
        np.m_History[0].m_Screen[0] = ps.x; np.m_History[0].m_Screen[1] = ps.y;
        wxColour c = GetSeaCurrentGraphicColor(vkn);
        np.m_History[0].m_Color[0] = c.Red(); np.m_History[0].m_Color[1] = c.Green(); np.m_History[0].m_Color[2] = c.Blue();

        // Pre-fill history with fake updates so trail is visible immediately
        float fakePos[2] = {p[0], p[1]};
        for (int h = 1; h < history_size && h < MAX_PARTICLE_HISTORY; h++) {
            double fakeVx = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridu, fakePos[0], fakePos[1], true);
            double fakeVy = gui->myMessage.getInterpolatedValue(gui->myMessage, gui->gridv, fakePos[0], fakePos[1], true);
            if (fakeVx != ncdf_NOTDEF && fakeVy != ncdf_NOTDEF && isfinite(fakeVx) && isfinite(fakeVy)) {
                double fakeMag = sqrt(fakeVx*fakeVx + fakeVy*fakeVy);
                double fakeAng = atan2(fakeVx, fakeVy) * 180.0 / PI;
                double fakeD = fakeMag * run_count * 1.94;  // Match GRIB
                float fakeAngr = (float)(fakeAng / 180.0 * PI);
                float fakeLatr = fakePos[1] * (float)PI / 180.0f;
                float fakeDD = (float)(fakeD / 3443.0);
                float sD = sinf(fakeDD), cD = cosf(fakeDD);
                float sy = sinf(fakeLatr), cy = cosf(fakeLatr);
                float sa = sinf(fakeAngr), ca = cosf(fakeAngr);
                fakePos[0] = fakePos[0] + asinf(sa * sD / cy) * 180.0f / (float)PI;
                fakePos[1] = asinf(sy * cD + cy * sD * ca) * 180.0f / (float)PI;
            }
            np.m_History[h].m_Pos[0] = fakePos[0];
            np.m_History[h].m_Pos[1] = fakePos[1];
            wxPoint fps;
            GetCanvasPixLL(vp, &fps, fakePos[1], fakePos[0]);
            np.m_History[h].m_Screen[0] = fps.x; np.m_History[h].m_Screen[1] = fps.y;
            wxColour fc = GetSeaCurrentGraphicColor(vkn);
            np.m_History[h].m_Color[0] = fc.Red(); np.m_History[h].m_Color[1] = fc.Green(); np.m_History[h].m_Color[2] = fc.Blue();
            np.m_HistorySize = h + 1;
            np.m_HistoryPos = h;
        }

        particles.push_back(np);
    }
    } // end shouldUpdate

    // Debug: log particle state
    static int s_pDbg = 0;
    if (s_pDbg < 5) {
        wxLogMessage(_T("[particle] count=%zu, m_pdc=%p, history_size=%d"),
            particles.size(), m_pdc, history_size);
        s_pDbg++;
    }

    // Render with GRIB-style interpolation (immediate mode GL, stable)
    if (!m_pdc && !particles.empty()) {
#ifdef ocpnUSE_GL
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(2.3f);

        for (auto it = particles.begin(); it != particles.end(); it++) {
            wxUint8 alpha = 250;
            int i = it->m_HistoryPos;

            bool lip_valid = false;
            float *lp = nullptr, lip[2];
            wxUint8 lc[4];

            // Velocity-dependent trail length: slow=short, fast=long
            // Scale alpha decay: lower velocity = faster decay = shorter trail
            double velFactor = wxMax(0.3, wxMin(3.0, it->m_Velocity * 2.0));
            int trailSteps = (int)(history_size * velFactor);
            trailSteps = wxMax(2, wxMin(trailSteps, MAX_PARTICLE_HISTORY));
            int alphaDecay = 250 / trailSteps;

            for (;;) {
                if (it->m_History[i].m_Pos[0] != -10000) {
                    float *sp = it->m_History[i].m_Screen;
                    wxUint8 *ci = it->m_History[i].m_Color;
                    wxUint8 c[4] = {ci[0], ci[1], ci[2], alpha};

                    if (lp && fabsf(lp[0] - sp[0]) < vp->pix_width) {
                        float sip[2];
                        float d = (float)it->m_Run / run_count;
                        sip[0] = d * lp[0] + (1 - d) * sp[0];
                        sip[1] = d * lp[1] + (1 - d) * sp[1];

                        if (lip_valid && fabsf(lip[0] - sip[0]) < vp->pix_width) {
                            glColor4ub(lc[0], lc[1], lc[2], lc[3]);
                            glBegin(GL_LINES);
                            glVertex2f(lip[0], lip[1]);
                            glVertex2f(sip[0], sip[1]);
                            glEnd();
                        }
                        lip[0] = sip[0]; lip[1] = sip[1];
                        lip_valid = true;
                    }
                    memcpy(lc, c, sizeof lc);
                    lp = sp;
                }

                if (--i < 0) {
                    i = history_size - 1;
                    if (i >= it->m_HistorySize) break;
                }
                if (i == it->m_HistoryPos) break;
                alpha -= alphaDecay;
            }
        }

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#ifdef __WXMSW__
        glFlush();
#endif
#endif
    }

    // Adaptive timer (GRIB formula)
    if (!m_tParticleTimer.IsRunning()) {
        m_tParticleTimer.Start(50, wxTIMER_ONE_SHOT);
    }
}

// Calculates if two boxes intersect. If so, the function returns _ON.
// If they do not intersect, two scenario's are possible:
// other is outside this -> return _OUT
// other is inside this -> return _IN
OVERLAP Intersect(PlugIn_ViewPort *vp,
       double lat_min, double lat_max, double lon_min, double lon_max, double Marge)
{

    if (((vp->lon_min - Marge) > (lon_max + Marge)) ||
         ((vp->lon_max + Marge) < (lon_min - Marge)) ||
         ((vp->lat_max + Marge) < (lat_min - Marge)) ||
         ((vp->lat_min - Marge) > (lat_max + Marge)))
        return _OUT;

    // Check if other.bbox is inside this bbox
    if ((vp->lon_min <= lon_min) &&
         (vp->lon_max >= lon_max) &&
         (vp->lat_max >= lat_max) &&
         (vp->lat_min <= lat_min))
        return _IN;

    // Boundingboxes intersect
    return _ON;
}
