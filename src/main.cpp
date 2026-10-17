#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <vector>

using namespace geode::prelude;

// Smart Perf
//  - Smart Culling   : particle milik object yang di luar layar di-pause & disembunyikan
//  - Particle Limiter: emission rate particle dikalikan faktor density
//                      (statis dari setting + dinamis mengikuti FPS)

class $modify(PerfPlayLayer, PlayLayer) {
    struct Entry {
        GameObject* obj = nullptr;
        CCParticleSystemQuad* ps = nullptr; // particle terakhir yang kita lihat
        float baseRate = 0.f;               // emission rate asli
        float appliedFactor = 1.f;          // faktor yang sedang dipakai
        bool culled = false;
    };

    struct Fields {
        std::vector<Entry> entries;
        float fpsAvg = 60.f;
        float dynDensity = 1.f; // 0..1, diatur otomatis
        int frame = 0;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        auto& entries = m_fields->entries;
        entries.clear();
        m_fields->fpsAvg = 60.f;
        m_fields->dynDensity = 1.f;
        m_fields->frame = 0;

        if (!m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (obj && obj->m_hasParticles) {
                Entry e;
                e.obj = obj;
                entries.push_back(e);
            }
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& f = *m_fields.self();
        if (f.entries.empty()) return;

        // hitung FPS halus dari delta time asli frame
        float real = CCDirector::get()->getDeltaTime();
        if (real > 0.0001f) {
            f.fpsAvg += ((1.f / real) - f.fpsAvg) * 0.05f;
        }

        // cek tiap 6 frame saja, biar murah
        if ((++f.frame % 6) != 0) return;

        auto mod = Mod::get();
        bool doCull = mod->getSettingValue<bool>("enable-culling");
        bool doLimit = mod->getSettingValue<bool>("enable-limiter");
        bool dynamic = mod->getSettingValue<bool>("dynamic-limiter");
        float margin = static_cast<float>(mod->getSettingValue<int64_t>("cull-margin"));
        float userDensity = mod->getSettingValue<int64_t>("particle-density") / 100.f;
        float targetFps = static_cast<float>(mod->getSettingValue<int64_t>("target-fps"));
        float minDensity = mod->getSettingValue<int64_t>("min-density") / 100.f;

        // atur density dinamis
        if (doLimit && dynamic) {
            if (f.fpsAvg < targetFps) {
                f.dynDensity = std::max(minDensity, f.dynDensity - 0.08f);
            } else if (f.fpsAvg > targetFps * 1.15f) {
                f.dynDensity = std::min(1.f, f.dynDensity + 0.04f);
            }
        } else {
            f.dynDensity = 1.f;
        }

        float factor = 1.f;
        if (doLimit) {
            factor = std::min(userDensity, f.dynDensity);
            factor = std::max(factor, 0.f);
        }

        auto win = CCDirector::get()->getWinSize();
        float layerX = m_objectLayer ? m_objectLayer->getPositionX() : 0.f;
        float scale = m_objectLayer ? m_objectLayer->getScale() : 1.f;

        for (auto& e : f.entries) {
            auto obj = e.obj;
            if (!obj) continue;

            auto ps = obj->m_particle;
            if (!ps) continue;

            // particle baru / dibuat ulang -> catat rate aslinya
            if (ps != e.ps) {
                e.ps = ps;
                e.baseRate = ps->getEmissionRate();
                e.appliedFactor = 1.f;
                e.culled = false;
            }

            // ---- Smart Culling ----
            bool shouldCull = false;
            if (doCull) {
                float sx = obj->getPositionX() * scale + layerX;
                shouldCull = (sx < -margin) || (sx > win.width + margin);
            }

            if (shouldCull && !e.culled) {
                ps->setVisible(false);
                ps->pauseSchedulerAndActions();
                e.culled = true;
            } else if (!shouldCull && e.culled) {
                ps->setVisible(true);
                ps->resumeSchedulerAndActions();
                e.culled = false;
            }

            // ---- Particle Limiter ----
            if (std::abs(e.appliedFactor - factor) > 0.001f) {
                ps->setEmissionRate(e.baseRate * factor);
                e.appliedFactor = factor;
            }
        }
    }
};
