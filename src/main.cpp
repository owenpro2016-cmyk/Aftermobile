#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Slider.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <array>
#include <cmath>
#include <functional>

using namespace geode::prelude;

namespace ia {

enum class Profile : int { Off, Soft, Balanced, Aggressive, Custom };
enum class AlignMode : int { Off, Aligned, Offset };

struct AssistConfig {
bool enabled = true;
Profile profile = Profile::Balanced;
AlignMode alignMode = AlignMode::Aligned;
int alignOffsetFrames = 0;
bool retainEnabled = true;
int retainWindowMs = 120;
int retainWindowFrames = 8;
bool adaptiveEnabled = false;
int adaptiveWindow = 45;
float adaptiveMaxStep = 0.08f;
bool waveEnabled = true;
int wavePulseTicks = 2;
bool flightEnabled = true;
float flightDamping = 0.55f;
int flightMaxCorrections = 8;
int obstacleRadius = 3;
bool showIndicator = true;
float indicatorX = 0.9f;
float indicatorY = 0.15f;
bool perLevelProfiles = false;static Profile parseProfile(std::string const& s) {
    if (s == "off") return Profile::Off;
    if (s == "soft") return Profile::Soft;
    if (s == "balanced") return Profile::Balanced;
    if (s == "aggressive") return Profile::Aggressive;
    return Profile::Custom;
}
static AlignMode parseAlignMode(std::string const& s) {
    if (s == "off") return AlignMode::Off;
    if (s == "offset") return AlignMode::Offset;
    return AlignMode::Aligned;
}

void applyProfile(Profile p) {
    profile = p;
    switch (p) {
        case Profile::Off:
            enabled = false; break;
        case Profile::Soft:
            enabled = true;
            alignMode = AlignMode::Aligned;
            alignOffsetFrames = 0;
            retainEnabled = true;
            retainWindowMs = 80;
            retainWindowFrames = 4;
            adaptiveEnabled = false;
            waveEnabled = false;
            flightEnabled = false;
            break;
        case Profile::Balanced:
            enabled = true;
            alignMode = AlignMode::Aligned;
            alignOffsetFrames = 1;
            retainEnabled = true;
            retainWindowMs = 120;
            retainWindowFrames = 6;
            waveEnabled = true;
            wavePulseTicks = 2;
            flightEnabled = true;
            flightDamping = 0.55f;
            flightMaxCorrections = 8;
            break;
        case Profile::Aggressive:
            enabled = true;
            alignMode = AlignMode::Offset;
            alignOffsetFrames = 2;
            retainEnabled = true;
            retainWindowMs = 160;
            retainWindowFrames = 10;
            waveEnabled = true;
            wavePulseTicks = 1;
            flightEnabled = true;
            flightDamping = 0.35f;
            flightMaxCorrections = 14;
            break;
        case Profile::Custom:
        default: break;
    }
}

static AssistConfig loadFromSettings() {
    AssistConfig c;
    auto m = Mod::get();
    c.enabled = m->getSettingValue<bool>("enabled");
    c.profile = parseProfile(m->getSettingValue<std::string>("profile"));
    c.alignMode = parseAlignMode(m->getSettingValue<std::string>("alignMode"));
    c.alignOffsetFrames = m->getSettingValue<int>("alignOffset");
    c.retainEnabled = m->getSettingValue<bool>("retainEnabled");
    c.retainWindowMs = m->getSettingValue<int>("retainWindowMs");
    c.retainWindowFrames = m->getSettingValue<int>("retainWindowFrames");
    c.adaptiveEnabled = m->getSettingValue<bool>("adaptiveEnabled");
    c.adaptiveWindow = m->getSettingValue<int>("adaptiveWindow");
    c.adaptiveMaxStep = (float)m->getSettingValue<double>("adaptiveMaxStep");
    c.waveEnabled = m->getSettingValue<bool>("waveEnabled");
    c.wavePulseTicks = m->getSettingValue<int>("wavePulseTicks");
    c.flightEnabled = m->getSettingValue<bool>("flightEnabled");
    c.flightDamping = (float)m->getSettingValue<double>("flightDamping");
    c.flightMaxCorrections = m->getSettingValue<int>("flightMaxCorrections");
    c.obstacleRadius = m->getSettingValue<int>("obstacleRadius");
    c.showIndicator = m->getSettingValue<bool>("showIndicator");
    c.indicatorX = (float)m->getSettingValue<double>("indicatorX");
    c.indicatorY = (float)m->getSettingValue<double>("indicatorY");
    c.perLevelProfiles = m->getSettingValue<bool>("perLevelProfiles");

    if (c.profile != Profile::Custom) c.applyProfile(c.profile);
    return c;
}
};

class FrameClock {
public:
void reset() { m_tick = 0; m_inUpdate = false; }
void beginUpdate() { m_inUpdate = true; }
void endUpdate() { m_inUpdate = false; ++m_tick; }
std::uint64_t tick() const { return m_tick; }
bool inUpdate() const { return m_inUpdate; }
std::uint64_t targetTick(int offsetFrames) const {
    std::int64_t earliest = (std::int64_t)m_tick + (m_inUpdate ? 1 : 0);
    if (earliest < (std::int64_t)m_tick) earliest = m_tick;
    std::int64_t target = earliest + offsetFrames;
    if (target < (std::int64_t)m_tick) target = (std::int64_t)m_tick;
    return (std::uint64_t)target;
}

static int msToFrames(int ms, int hz = 60) {
    int f = (int)(((long long)ms * hz) / 1000);
    return f < 1 ? 1 : f;
}
private:
std::uint64_t m_tick = 0;
bool m_inUpdate = false;
};

struct BufferedInput {
PlayerButton button = PlayerButton::Jump;
bool isPress = false;
bool secondPlayer = false;
std::uint64_t capturedTick = 0;
std::uint64_t dueTick = 0;
bool forcedDelay = false;
bool consumed = false;
std::uint16_t id = 0;
};

class InputBuffer {
public:
static constexpr std::size_t CAPACITY = 64;
void reset() { m_count = 0; m_nextId = 1; }

bool push(BufferedInput in) {
    if (m_count >= CAPACITY) return false;
    in.id = m_nextId++;
    if (m_nextId == 0) m_nextId = 1;
    m_pending[m_count++] = in;
    return true;
}

std::size_t size() const { return m_count; }
BufferedInput& at(std::size_t i) { return m_pending[i]; }
const BufferedInput& at(std::size_t i) const { return m_pending[i]; }

void removeAt(std::size_t i) {
    if (i >= m_count) return;
    for (std::size_t j = i; j + 1 < m_count; ++j)
        m_pending[j] = m_pending[j + 1];
    --m_count;
}

template <typename Fn>
int drainReady(std::uint64_t tickNow, Fn&& fn) {
    int consumed = 0;
    std::size_t i = 0;
    while (i < m_count) {
        if (!m_pending[i].consumed && m_pending[i].dueTick <= tickNow) {
            BufferedInput copy = m_pending[i];
            fn(copy);
            removeAt(i);
            ++consumed;
        } else ++i;
    }
    return consumed;
}

int pruneOlderThan(std::uint64_t tickMin) {
    int n = 0;
    std::size_t i = 0;
    while (i < m_count) {
        if (m_pending[i].dueTick < tickMin && m_pending[i].forcedDelay) {
            removeAt(i); ++n;
        } else ++i;
    }
    return n;
}

bool hasHeld(PlayerButton b, bool second) const {
    for (std::size_t i = 0; i < m_count; ++i) {
        auto& e = m_pending[i];
        if (e.button == b && e.secondPlayer == second && e.isPress && !e.consumed)
            return true;
    }
    return false;
}
private:
std::array<BufferedInput, CAPACITY> m_pending{};
std::size_t m_count = 0;
std::uint16_t m_nextId = 1;
};
enum class ObstacleKind : std::uint8_t {
Solid, Slope, Hazard, Orb, Pad, Portal, Ice, Other
};

struct ObstacleInfo {
cocos2d::CCPoint position{0.f, 0.f};
ObstacleKind kind = ObstacleKind::Other;
int objectId = 0;
};

struct ObstacleReport {
static constexpr int MAX_ITEMS = 48;
std::array<ObstacleInfo, MAX_ITEMS> items{};
int count = 0;
int solidCount = 0;
int hazardCount = 0;
int orbCount = 0;
int padCount = 0;
float nearestHazardDx = 1e9f;
float nearestSolidDx = 1e9f;
float nearestOrbDy = 0.f;
float nearestOrbDx = 1e9f;
};

class ObstacleScanner {
public:
static constexpr float kBlock = 30.f;
void scan(GJBaseGameLayer* layer, PlayerObject* player,
          float radiusBlocks, ObstacleReport& out) const {
    out = ObstacleReport{};
    if (!layer || !player) return;
    auto* arr = layer->m_objects;
    if (!arr) return;

    const auto ppos = player->getPosition();
    const float radius = radiusBlocks * kBlock;
    const float padding = 3.f * kBlock;
    const unsigned n = arr->count();

    for (unsigned i = 0; i < n; ++i) {
        auto* obj = static_cast<GameObject*>(arr->objectAtIndex(i));
        if (!obj) continue;
        const auto pos = obj->getPosition();
        const float dx = pos.x - ppos.x;
        const float dy = pos.y - ppos.y;
        if (dx < -padding) continue;
        if (dx > radius) continue;
        if (std::abs(dy) > radius * 1.5f) continue;

        const auto kind = classify(obj);
        if (kind == ObstacleKind::Other) continue;

        if (out.count < ObstacleReport::MAX_ITEMS) {
            ObstacleInfo info;
            info.position = pos;
            info.kind = kind;
            info.objectId = obj->m_objectID;
            out.items[out.count++] = info;

            switch (kind) {
                case ObstacleKind::Solid:
                case ObstacleKind::Slope:
                    out.solidCount++;
                    if (dx < out.nearestSolidDx) out.nearestSolidDx = dx;
                    break;
                case ObstacleKind::Hazard:
                    out.hazardCount++;
                    if (dx < out.nearestHazardDx) out.nearestHazardDx = dx;
                    break;
                case ObstacleKind::Orb:
                    out.orbCount++;
                    if (dx < out.nearestOrbDx) {
                        out.nearestOrbDx = dx;
                        out.nearestOrbDy = dy;
                    }
                    break;
                case ObstacleKind::Pad:
                    out.padCount++;
                    break;
                default: break;
            }
        }
    }
}
private:
static ObstacleKind classify(GameObject* obj) {
if (!obj) return ObstacleKind::Other;
const int id = obj->m_objectID;
const auto type = obj->m_objectType;
switch (id) {
case 36: case 41: case 42: case 43: case 44: case 45:
case 46: case 47: case 48: case 151: case 152:
case 153: case 1704: case 1705: case 1706: case 1707:
return ObstacleKind::Orb;
default: break;
}
if (id == 35 || id == 67 || id == 140 || id == 1331 || id == 1332
|| id == 1333 || id == 1704) return ObstacleKind::Pad;
if (type == GameObjectType::Hazard) return ObstacleKind::Hazard;
if (type == GameObjectType::Solid) return ObstacleKind::Solid;
if (type == GameObjectType::Slope) return ObstacleKind::Slope;
if (type == GameObjectType::Modifier) return ObstacleKind::Portal;
return ObstacleKind::Other;
}
};

struct TrajectorySample {
cocos2d::CCPoint position{0.f, 0.f};
float yVelocity = 0.f;
bool wouldHitSolid = false;
bool wouldHitHazard = false;
};

struct TrajectoryResult {
static constexpr int MAX_STEPS = 20;
std::array<TrajectorySample, MAX_STEPS> samples{};
int steps = 0;
int firstSolidTick = -1;
int firstHazardTick = -1;
};

class TrajectorySolver {
public:
void reset() {
m_layer = nullptr;
m_yVel = 0.f;
m_upsideDown = m_ship = m_ufo = m_wave = m_ball = m_robot = m_spider = false;
}
void captureBaseline(PlayerObject* p, GJBaseGameLayer* layer) {
    if (!p) return;
    m_layer = layer;
    m_start = p->getPosition();
    m_yVel = (float)p->m_yVelocity;
    m_upsideDown = p->m_isUpsideDown;
    m_ship = p->m_isShip;
    m_ufo = p->m_isBird;
    m_wave = p->m_isDart;
    m_ball = p->m_isBall;
    m_robot = p->m_isRobot;
    m_spider = p->m_isSpider;
}

void predict(const bool* states, int ticks, TrajectoryResult& out) const {
    out = TrajectoryResult{};
    TrajectorySample s;
    s.position = m_start;
    s.yVelocity = m_yVel;

    for (int i = 0; i < TrajectoryResult::MAX_STEPS; ++i) {
        if (i >= ticks) break;
        const bool down = states ? states[i] : false;
        stepOnce(s, down);
        out.samples[i] = s;
        out.steps = i + 1;
        bool solid = false, hazard = false;
        queryCollision(s, solid, hazard);
        out.samples[i].wouldHitSolid = solid;
        out.samples[i].wouldHitHazard = hazard;
        if (solid && out.firstSolidTick < 0) out.firstSolidTick = i;
        if (hazard && out.firstHazardTick < 0) out.firstHazardTick = i;
        if (solid) break;
    }
}

void predictNeutral(int ticks, TrajectoryResult& out) const { predict(nullptr, ticks, out); }
private:
void stepOnce(TrajectorySample& s, bool down) const {
const float dirG = m_upsideDown ? -1.f : 1.f;
if (m_wave) {
s.yVelocity = down ? -6.f : 6.f;
s.position.y += s.yVelocity;
return;
}
if (m_ship) {
const float target = down ? -8.f : 8.f;
s.yVelocity += (target - s.yVelocity) * 0.25f;
s.position.y += s.yVelocity;
return;
}
if (m_ufo) {
if (down && s.yVelocity > -4.f) s.yVelocity -= 4.5f;
s.yVelocity += 0.9f * dirG;
s.position.y += s.yVelocity;
return;
}
if (m_ball) {
if (down) s.yVelocity = 12.f * -dirG;
s.yVelocity += 1.6f * dirG;
s.position.y += s.yVelocity;
return;
}
if (m_robot) {
if (down) s.yVelocity = 14.f * -dirG;
s.yVelocity += 1.5f * dirG;
s.position.y += s.yVelocity;
return;
}
if (m_spider) {
if (down) s.yVelocity = 12.f * -dirG;
s.yVelocity += 1.6f * dirG;
s.position.y += s.yVelocity;
return;
}
if (down) s.yVelocity = 16.f * -dirG;
s.yVelocity += 1.55f * dirG;
s.position.y += s.yVelocity;
}
void queryCollision(const TrajectorySample& s, bool& solid, bool& hazard) const {
    solid = false; hazard = false;
    if (!m_layer) return;
    auto* arr = m_layer->m_objects;
    if (!arr) return;
    const float px = s.position.x, py = s.position.y;
    const float pad = 15.f;
    const unsigned n = arr->count();
    for (unsigned i = 0; i < n; ++i) {
        auto* obj = static_cast<GameObject*>(arr->objectAtIndex(i));
        if (!obj) continue;
        auto gpos = obj->getPosition();
        if (std::abs(gpos.x - px) > 400.f) continue;
        if (std::abs(gpos.y - py) > 200.f) continue;
        const auto t = obj->m_objectType;
        const bool isS = (t == GameObjectType::Solid);
        const bool isH = (t == GameObjectType::Hazard);
        if (!isS && !isH) continue;
        const float half = 15.f;
        if (std::abs(gpos.x - px) < pad + half && std::abs(gpos.y - py) < pad + half) {
            if (isS) solid = true;
            if (isH) hazard = true;
            if (solid) return;
        }
    }
}

GJBaseGameLayer* m_layer = nullptr;
cocos2d::CCPoint m_start{0.f, 0.f};
float m_yVel = 0.f;
bool m_upsideDown = false;
bool m_ship = false;
bool m_ufo = false;
bool m_wave = false;
bool m_ball = false;
bool m_robot = false;
bool m_spider = false;
};

struct AssistMetrics {
int totalClicks = 0;
int corrected = 0;
int discarded = 0;
float meanErrorFrames = 0.f;
float stdErrorFrames = 0.f;
float adaptiveOffset = 0.f;
};

class CorrectionEngine {
public:
void reset() {
m_metrics = AssistMetrics{};
m_adaptiveOffset = 0.f;
m_errorCount = 0;
m_errorHead = 0;
m_clicksSinceEval = 0;
m_lastScore = 0.f;
m_adjustDirection = 1;
m_targetCaptured = false;
m_targetY = 0.f;
m_lastEmittedHold = false;
m_lastHoldTick = 0;
m_lastVelocity = 0.f;
m_lastWasWave = false;
}
void configure(const AssistConfig& c) {
    m_cfg = c;
    if (m_cfg.adaptiveWindow <= 0) m_cfg.adaptiveWindow = 45;
}

void setAdaptiveOffset(float f) { m_adaptiveOffset = f; }
float adaptiveOffset() const { return m_adaptiveOffset; }

const AssistMetrics& metrics() const { return m_metrics; }
AssistMetrics& metricsMut() { return m_metrics; }

template <typename EmitPress, typename EmitRelease>
void resolveTick(PlayLayer* pl, FrameClock& clock, InputBuffer& buffer,
                 TrajectorySolver& solver, ObstacleScanner& scanner,
                 EmitPress&& emitPress, EmitRelease&& emitRelease) {
    if (!pl) return;
    const auto tick = clock.tick();
    const bool engineActive = m_cfg.enabled && m_cfg.alignMode != AlignMode::Off;

    if (m_cfg.retainEnabled) {
        const int w = m_cfg.retainWindowFrames;
        const std::uint64_t cutoff = (tick > (std::uint64_t)w) ? tick - w : 0;
        int pruned = buffer.pruneOlderThan(cutoff);
        if (pruned > 0) {
            m_metrics.discarded += pruned;
            m_metrics.totalClicks += pruned;
        }
    }

    if (!engineActive) {
        buffer.drainReady(tick, [&](const BufferedInput& in) {
            if (in.isPress) emitPress(in.button, in.secondPlayer);
            else emitRelease(in.button, in.secondPlayer);
        });
        return;
    }

    for (std::size_t i = 0; i < buffer.size(); ) {
        BufferedInput e = buffer.at(i);
        if (e.consumed || e.dueTick > tick) { ++i; continue; }

        bool safe = true;
        if (m_cfg.retainEnabled) safe = isSafeToEmit(e, pl, solver, scanner);

        if (safe) {
            if (e.isPress) emitPress(e.button, e.secondPlayer);
            else emitRelease(e.button, e.secondPlayer);
            if (e.forcedDelay) {
                m_metrics.corrected++;
                pushError((float)(e.dueTick - e.capturedTick));
            }
            m_metrics.totalClicks++;
            buffer.removeAt(i);
        } else {
            if (!canDelayFurther(e, tick)) {
                m_metrics.discarded++;
                m_metrics.totalClicks++;
                buffer.removeAt(i);
            } else {
                buffer.at(i).dueTick = tick + 1;
                buffer.at(i).forcedDelay = true;
                ++i;
            }
        }
    }
}
template <typename ApplyHold>
void updateVerticalAssist(PlayerObject* p, FrameClock& clock,
                          ObstacleScanner& scanner, ApplyHold&& applyHold) {
    if (!p) return;
    const bool isWave = p->m_isDart;
    const bool isShip = p->m_isShip;
    const bool isUfo  = p->m_isBird;
    if (!(isWave && m_cfg.waveEnabled) &&
        !(isShip && m_cfg.flightEnabled) &&
        !(isUfo  && m_cfg.flightEnabled)) return;

    if (m_layerForScan) {
        ObstacleReport r;
        scanner.scan(m_layerForScan, p, 2.f, r);
        if (r.nearestSolidDx < 60.f) {
            m_targetCaptured = false;
            return;
        }
    }

    if (!m_targetCaptured) {
        m_targetY = p->getPositionY();
        m_targetCaptured = true;
        m_lastHoldTick = clock.tick();
    }
    if (isWave != m_lastWasWave) {
        m_targetY = p->getPositionY();
        m_lastWasWave = isWave;
    }

    const float y = p->getPositionY();
    const float err = m_targetY - y;
    const float thresh = 6.f;
    bool wantPress = m_lastEmittedHold;

    if (isWave) {
        const int period = m_cfg.wavePulseTicks > 0 ? m_cfg.wavePulseTicks : 2;
        wantPress = ((clock.tick() / period) & 1) == 0;
        if (err > thresh) wantPress = true;
        else if (err < -thresh) wantPress = false;
    } else if (isShip) {
        const float v = m_lastVelocity;
        const float lookahead = y + v * 3.f;
        const float terr = m_targetY - lookahead;
        if (terr > thresh) wantPress = true;
        else if (terr < -thresh) wantPress = false;
    } else if (isUfo) {
        const float v = m_lastVelocity;
        const float predict = y + v * 2.f - 3.f;
        wantPress = predict < (m_targetY - thresh);
    }

    const auto now = clock.tick();
    const double secs = (double)(now - m_lastHoldTick) / 60.0;
    if (wantPress != m_lastEmittedHold) {
        if (secs * (double)m_cfg.flightMaxCorrections >= 1.0 || m_lastHoldTick == 0) {
            applyHold(wantPress);
            m_lastEmittedHold = wantPress;
            m_lastHoldTick = now;
        }
    }

    m_lastVelocity = (float)p->m_yVelocity;
    m_lastWasWave = isWave;
}

void setScanLayer(GJBaseGameLayer* l) { m_layerForScan = l; }
void markTarget(float y) { m_targetY = y; m_targetCaptured = true; }
void releaseTarget() { m_targetCaptured = false; }

void endTick() {
    if (!m_cfg.adaptiveEnabled) return;
    if (m_errorCount < 10) return;

    float sum = 0.f, sumSq = 0.f;
    for (int i = 0; i < m_errorCount; ++i) {
        const float v = m_errorWindow[i];
        sum += v; sumSq += v * v;
    }
    const float n = (float)m_errorCount;
    const float mean = sum / n;
    const float var = std::max(0.f, sumSq / n - mean * mean);
    m_metrics.meanErrorFrames = mean;
    m_metrics.stdErrorFrames = std::sqrt(var);

    m_clicksSinceEval++;
    if (m_clicksSinceEval < m_cfg.adaptiveWindow) return;
    m_clicksSinceEval = 0;

    const float score = -(std::abs(mean) + 0.25f * std::sqrt(var));
    if (m_lastScore != 0.f && std::abs(score - m_lastScore) < 1e-4f)
        m_adjustDirection = -m_adjustDirection;
    else if (m_lastScore != 0.f && score < m_lastScore - 0.02f)
        m_adjustDirection = -m_adjustDirection;
    m_lastScore = score;

    m_adaptiveOffset += (float)m_adjustDirection * m_cfg.adaptiveMaxStep;
    if (m_adaptiveOffset >  3.f) m_adaptiveOffset =  3.f;
    if (m_adaptiveOffset < -3.f) m_adaptiveOffset = -3.f;
    m_metrics.adaptiveOffset = m_adaptiveOffset;

    m_errorCount = 0;
    m_errorHead = 0;
}
private:
void pushError(float v) {
if (m_cfg.adaptiveWindow <= 0) return;
const int cap = std::min(m_cfg.adaptiveWindow, (int)ADAPTIVE_MAX);
if (m_errorCount < cap) {
m_errorWindow[m_errorHead] = v;
m_errorHead = (m_errorHead + 1) % cap;
m_errorCount++;
} else {
m_errorWindow[m_errorHead] = v;
m_errorHead = (m_errorHead + 1) % cap;
}
}
bool canDelayFurther(const BufferedInput& e, std::uint64_t tick) const {
    if (!m_cfg.retainEnabled) return false;
    const int cap = std::min(m_cfg.retainWindowFrames,
                             FrameClock::msToFrames(m_cfg.retainWindowMs));
    const auto elapsed = (tick > e.capturedTick) ? (tick - e.capturedTick) : 0;
    return (int)elapsed < cap;
}

bool isSafeToEmit(const BufferedInput& e, PlayLayer* pl,
                  TrajectorySolver& solver, ObstacleScanner& scanner) {
    if (!pl) return true;
    auto* p = e.secondPlayer ? pl->m_player2 : pl->m_player1;
    if (!p) return true;
    if (!e.isPress) return true;

    ObstacleReport r;
    scanner.scan(pl, p, (float)m_cfg.obstacleRadius, r);
    if (r.hazardCount == 0) return true;

    solver.captureBaseline(p, pl);
    bool states[TrajectoryResult::MAX_STEPS] = {};
    for (int i = 0; i < TrajectoryResult::MAX_STEPS; ++i) states[i] = true;
    TrajectoryResult tr;
    solver.predict(states, 15, tr);
    if (tr.firstHazardTick >= 0 && tr.firstHazardTick < 6) return false;
    return true;
}

AssistConfig m_cfg{};
AssistMetrics m_metrics{};
static constexpr int ADAPTIVE_MAX = 80;
std::array<float, ADAPTIVE_MAX> m_errorWindow{};
int m_errorCount = 0;
int m_errorHead = 0;
float m_adaptiveOffset = 0.f;
int m_clicksSinceEval = 0;
float m_lastScore = 0.f;
int m_adjustDirection = 1;

GJBaseGameLayer* m_layerForScan = nullptr;
float m_targetY = 0.f;
bool m_targetCaptured = false;
float m_lastVelocity = 0.f;
bool m_lastEmittedHold = false;
std::uint64_t m_lastHoldTick = 0;
bool m_lastWasWave = false;
};

class AssistState {
public:
static AssistState& get() { static AssistState s; return s; }
void initialize() { reloadConfig(); }

void reloadConfig() {
    m_cfg = AssistConfig::loadFromSettings();
    m_engine.configure(m_cfg);
    m_engine.setScanLayer(m_pl);
}

void onLevelStart(PlayLayer* pl) {
    m_pl = pl;
    m_clock.reset();
    m_buffer.reset();
    m_engine.reset();
    m_engine.configure(m_cfg);
    m_engine.setScanLayer(pl);
    m_active = m_cfg.enabled;

    if (m_cfg.perLevelProfiles && pl && pl->m_level) {
        auto key = fmt::format("adaptive.{}", pl->m_level->m_levelID);
        float saved = Mod::get()->getSavedValue<float>(key, 0.f);
        m_engine.setAdaptiveOffset(saved);
    }
}

void onLevelEnd() {
    if (m_cfg.perLevelProfiles && m_pl && m_pl->m_level) {
        auto key = fmt::format("adaptive.{}", m_pl->m_level->m_levelID);
        Mod::get()->setSavedValue<float>(key, m_engine.adaptiveOffset());
    }
    m_active = false;
    m_pl = nullptr;
    m_buffer.reset();
    m_engine.reset();
}

void onPause(bool paused) { m_paused = paused; }

void preUpdate(PlayLayer* pl) {
    if (pl != m_pl || m_paused || !m_active) return;
    m_clock.beginUpdate();

    m_engine.resolveTick(pl, m_clock, m_buffer, m_solver, m_scanner,
        [&](PlayerButton b, bool second) {
            auto* p = second ? pl->m_player2 : pl->m_player1;
            if (p) emitPress(p, b);
        },
        [&](PlayerButton b, bool second) {
            auto* p = second ? pl->m_player2 : pl->m_player1;
            if (p) emitRelease(p, b);
        });

    auto* p1 = pl->m_player1;
    if (p1) {
        m_engine.updateVerticalAssist(p1, m_clock, m_scanner,
            [&](bool pressed) {
                m_bypass = true;
                if (pressed) p1->pushButton(PlayerButton::Jump);
                else         p1->releaseButton(PlayerButton::Jump);
                m_bypass = false;
            });
    }
}

void postUpdate(PlayLayer* pl) {
    if (pl != m_pl || m_paused || !m_active) return;
    m_engine.endTick();
    m_clock.endUpdate();
}

bool shouldIntercept() const {
    if (!m_active || m_bypass) return false;
    if (!m_cfg.enabled) return false;
    if (m_cfg.alignMode == AlignMode::Off) return false;
    return true;
}

void recordInput(PlayerObject* po, PlayerButton b, bool press) {
    if (!m_pl || !po) return;
    const bool second = (m_pl->m_player2 == po);
    const int offset = (m_cfg.alignMode == AlignMode::Offset)
                       ? m_cfg.alignOffsetFrames : 0;
    BufferedInput bi;
    bi.button = b;
    bi.isPress = press;
    bi.secondPlayer = second;
    bi.capturedTick = m_clock.tick();
    bi.dueTick = m_clock.targetTick(offset);

    if (m_cfg.adaptiveEnabled) {
        const int extra = (int)std::lround(m_engine.adaptiveOffset());
        if (extra != 0) {
            std::int64_t t = (std::int64_t)bi.dueTick + extra;
            if (t < (std::int64_t)m_clock.tick()) t = (std::int64_t)m_clock.tick();
            bi.dueTick = (std::uint64_t)t;
        }
    }

    if (!m_buffer.push(bi)) {
        if (press) emitPress(po, b);
        else emitRelease(po, b);
    }
}

void emitPress(PlayerObject* po, PlayerButton b) {
    if (!po) return;
    m_bypass = true;
    po->pushButton(b);
    m_bypass = false;
}
void emitRelease(PlayerObject* po, PlayerButton b) {
    if (!po) return;
    m_bypass = true;
    po->releaseButton(b);
    m_bypass = false;
}

void openPanel();
void resetAdaptive() {
    m_engine.reset();
    m_engine.configure(m_cfg);
    if (m_cfg.perLevelProfiles && m_pl && m_pl->m_level) {
        auto key = fmt::format("adaptive.{}", m_pl->m_level->m_levelID);
        Mod::get()->setSavedValue<float>(key, 0.f);
    }
}
void setIndicatorPos(float x, float y) {
    m_cfg.indicatorX = std::clamp(x, 0.02f, 0.98f);
    m_cfg.indicatorY = std::clamp(y, 0.02f, 0.98f);
    Mod::get()->setSavedValue<float>("indicator.x", m_cfg.indicatorX);
    Mod::get()->setSavedValue<float>("indicator.y", m_cfg.indicatorY);
}

const AssistConfig& config() const { return m_cfg; }
const AssistMetrics& metrics() const { return m_engine.metrics(); }
PlayLayer* currentLayer() const { return m_pl; }
bool isActive() const { return m_active; }
bool isBypass() const { return m_bypass; }
private:
AssistState() = default;
AssistConfig m_cfg{};
FrameClock m_clock;
InputBuffer m_buffer;
TrajectorySolver m_solver;
ObstacleScanner m_scanner;
CorrectionEngine m_engine;
PlayLayer* m_pl = nullptr;
bool m_active = false;
bool m_bypass = false;
bool m_paused = false;
};

class AssistUI : public geode::Popup<> {
public:
static AssistUI* create() {
auto* r = new AssistUI();
if (r && r->init()) { r->autorelease(); return r; }
CC_SAFE_DELETE(r);
return nullptr;
}

protected:
bool init() override {
if (!Popup<>::initWithContentSizeAndTitle({360.f, 340.f}, "InputAssist"))
return false;
auto* layer = m_mainLayer;
auto& cfg = AssistState::get().config();
    auto* menu = CCMenu::create();
    menu->setPosition({180.f, 280.f});
    menu->setContentSize({340.f, 44.f});
    layer->addChild(menu);

    const char* names[] = {"off", "soft", "balanced", "aggressive"};
    const char* labels[] = {"Off", "Suave", "Equilibrado", "Agresivo"};
    float x = -150.f;
    for (int i = 0; i < 4; ++i) {
        auto* lab = CCLabelBMFont::create(labels[i], "bigFont.fnt");
        lab->setScale(0.55f);
        auto* item = CCMenuItemSpriteExtra::create(
            lab, nullptr, this, menu_selector(AssistUI::onPreset));
        item->setTag(i);
        item->setPosition({x, 0.f});
        menu->addChild(item);
        x += 88.f;
    }

    auto* container = CCNode::create();
    container->setContentSize({340.f, 180.f});
    container->setPosition({180.f, 160.f});
    layer->addChild(container);

    auto addSlider = [&](const char* text, float y, int tag,
                         float min, float max, float val) {
        auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(0.42f);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition({-150.f, y + 12.f});
        container->addChild(l);

        auto* s = geode::SliderNode::create({290.f, 20.f});
        s->setMin(min);
        s->setMax(max);
        s->setValue(val);
        s->setTag(tag);
        s->setPosition({10.f, y - 6.f});
        container->addChild(s);
    };

    addSlider("Reten. frames",       60.f,  1, 1.f,  30.f,  (float)cfg.retainWindowFrames);
    addSlider("Reten. ms",           20.f,  2, 20.f, 300.f, (float)cfg.retainWindowMs);
    addSlider("Desfase frames",     -20.f,  3, -3.f, 3.f,   (float)cfg.alignOffsetFrames);
    addSlider("Pulso wave",         -60.f,  4, 1.f,  4.f,   (float)cfg.wavePulseTicks);
    addSlider("Radio obstaculos",  -100.f,  5, 1.f,  5.f,   (float)cfg.obstacleRadius);

    auto* bm = CCMenu::create();
    bm->setPosition({180.f, 30.f});
    layer->addChild(bm);

    auto* applyL = CCLabelBMFont::create("Aplicar", "bigFont.fnt");
    applyL->setScale(0.55f);
    auto* applyB = CCMenuItemSpriteExtra::create(
        applyL, nullptr, this, menu_selector(AssistUI::onApply));
    applyB->setPosition({-70.f, 0.f});
    bm->addChild(applyB);

    auto* resetL = CCLabelBMFont::create("Reset adaptativo", "bigFont.fnt");
    resetL->setScale(0.5f);
    auto* resetB = CCMenuItemSpriteExtra::create(
        resetL, nullptr, this, menu_selector(AssistUI::onReset));
    resetB->setPosition({70.f, 0.f});
    bm->addChild(resetB);

    m_metricsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_metricsLabel->setScale(0.5f);
    m_metricsLabel->setAnchorPoint({0.5f, 1.f});
    m_metricsLabel->setPosition({180.f, 300.f});
    layer->addChild(m_metricsLabel);

    refresh();
    schedule(schedule_selector(AssistUI::onTickRefresh), 0.25f);
    return true;
}

void onTickRefresh(float) { refresh(); }

void refresh() {
    if (!m_metricsLabel) return;
    const auto& m = AssistState::get().metrics();
    m_metricsLabel->setString(fmt::format(
        "clicks {} | corr {} | desc {} | err {:.2f} sf {:.2f}",
        m.totalClicks, m.corrected, m.discarded,
        m.meanErrorFrames, m.stdErrorFrames).c_str());
}

void onPreset(cocos2d::CCObject* s) {
    auto* it = static_cast<CCMenuItemSpriteExtra*>(s);
    if (!it) return;
    const int tag = it->getTag();
    const char* name = (tag == 0) ? "off"
                    : (tag == 1) ? "soft"
                    : (tag == 2) ? "balanced" : "aggressive";
    Mod::get()->setSettingValue<std::string>("profile", name);
    AssistState::get().reloadConfig();
    Notification::create(fmt::format("Perfil: {}", name),
                         NotificationIcon::Info)->show();
}

void onApply(cocos2d::CCObject*) {
    std::function<void(CCNode*)> walk = [&](CCNode* n) {
        if (auto* s = typeinfo_cast<geode::SliderNode*>(n)) {
            const int tag = s->getTag();
            const int v = (int)std::lround(s->getValue());
            switch (tag) {
                case 1: Mod::get()->setSettingValue<int>("retainWindowFrames", v); break;
                case 2: Mod::get()->setSettingValue<int>("retainWindowMs", v); break;
                case 3: Mod::get()->setSettingValue<int>("alignOffset", v); break;
                case 4: Mod::get()->setSettingValue<int>("wavePulseTicks", v); break;
                case 5: Mod::get()->setSettingValue<int>("obstacleRadius", v); break;
                default: break;
            }
        }
        for (auto* c : n->getChildrenExt()) walk(c);
    };
    if (m_mainLayer) walk(m_mainLayer);
    AssistState::get().reloadConfig();
    Notification::create("Ajustes aplicados", NotificationIcon::Success)->show();
}

void onReset(cocos2d::CCObject*) {
    AssistState::get().resetAdaptive();
    Notification::create("Modo adaptativo reseteado",
                         NotificationIcon::Info)->show();
    refresh();
}

cocos2d::CCLabelBMFont* m_metricsLabel = nullptr;
};

void AssistState::openPanel() {
if (auto* p = AssistUI::create()) p->show();
}

class AssistIndicator : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
static AssistIndicator& get() { static AssistIndicator s; return s; }
void attachTo(PlayLayer* pl) {
    if (!pl) return;
    m_pl = pl;
    if (!m_inited) {
        m_bg = CCNode::create();
        m_bg->setContentSize({160.f, 44.f});

        auto* bg = cocos2d::extension::CCScale9Sprite::create("square02_small.png");
        if (bg) {
            bg->setContentSize({160.f, 44.f});
            bg->setPosition({80.f, 22.f});
            bg->setOpacity(120);
            m_bg->addChild(bg, -1);
        }

        m_label = CCLabelBMFont::create("IA: --", "chatFont.fnt");
        m_label->setScale(0.45f);
        m_label->setAnchorPoint({0.5f, 0.5f});
        m_label->setPosition({80.f, 22.f});
        m_bg->addChild(m_label);
        m_inited = true;
    }
    pl->addChild(m_bg, 10000);
    const auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
    const auto& cfg = AssistState::get().config();
    m_bg->setPosition({win.width * cfg.indicatorX - 80.f,
                       win.height * cfg.indicatorY - 22.f});
    schedule(schedule_selector(AssistIndicator::tickUpdate), 0.2f);
}

void detach() {
    unscheduleAllSelectors();
    if (m_bg && m_bg->getParent()) m_bg->removeFromParent();
    m_pl = nullptr;
}

void tickUpdate(float) {
    if (!m_label) return;
    auto& st = AssistState::get();
    const auto& m = st.metrics();
    const auto& cfg = st.config();
    m_label->setString(fmt::format(
        "IA {} | c/s {}/{}",
        cfg.enabled ? "on" : "off",
        m.corrected, m.totalClicks).c_str());
}

bool ccTouchBegan(cocos2d::CCTouch* t, cocos2d::CCEvent*) override {
    if (!m_bg) return false;
    const auto p = t->getLocation();
    if (!m_bg->getBoundingBox().containsPoint(p)) return false;
    m_lastPos = p;
    m_dragging = true;
    return true;
}
void ccTouchMoved(cocos2d::CCTouch* t, cocos2d::CCEvent*) override {
    if (!m_dragging || !m_bg) return;
    const auto p = t->getLocation();
    auto pos = m_bg->getPosition();
    pos.x += p.x - m_lastPos.x;
    pos.y += p.y - m_lastPos.y;
    m_lastPos = p;
    const auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
    pos.x = std::clamp(pos.x, 0.f, win.width - 160.f);
    pos.y = std::clamp(pos.y, 0.f, win.height - 44.f);
    m_bg->setPosition(pos);
}
void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override {
    if (!m_dragging || !m_bg) return;
    m_dragging = false;
    const auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
    const auto pos = m_bg->getPosition();
    AssistState::get().setIndicatorPos(
        (pos.x + 80.f) / win.width,
        (pos.y + 22.f) / win.height);
}
void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override {
    m_dragging = false;
}
private:
AssistIndicator() = default;
cocos2d::CCLabelBMFont* m_label = nullptr;
cocos2d::CCNode* m_bg = nullptr;
PlayLayer* m_pl = nullptr;
cocos2d::CCPoint m_lastPos{0.f, 0.f};
bool m_dragging = false;
bool m_inited = false;
};

} // namespace ia

class $modify(IA_PlayerObject, PlayerObject) {
void pushButton(PlayerButton b) {
auto& st = ia::AssistState::get();
if (st.shouldIntercept()) { st.recordInput(this, b, true); return; }
PlayerObject::pushButton(b);
}
void releaseButton(PlayerButton b) {
auto& st = ia::AssistState::get();
if (st.shouldIntercept()) { st.recordInput(this, b, false); return; }
PlayerObject::releaseButton(b);
}
};

class $modify(IA_PlayLayer, PlayLayer) {
bool init(GJGameLevel* level, bool useReplay, bool dontSelfInit) {
if (!PlayLayer::init(level, useReplay, dontSelfInit)) return false;
ia::AssistState::get().onLevelStart(this);
if (ia::AssistState::get().config().showIndicator) {
ia::AssistIndicator::get().attachTo(this);
}
return true;
}
void update(float dt) {
    ia::AssistState::get().preUpdate(this);
    PlayLayer::update(dt);
    ia::AssistState::get().postUpdate(this);
}

void pauseGame(bool pause) {
    ia::AssistState::get().onPause(pause);
    PlayLayer::pauseGame(pause);
}

void onQuit() {
    ia::AssistIndicator::get().detach();
    ia::AssistState::get().onLevelEnd();
    PlayLayer::onQuit();
}
};

class $modify(IA_PauseLayer, PauseLayer) {
void customSetup() {
PauseLayer::customSetup();
const auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
    auto* label = CCLabelBMFont::create("Assist", "bigFont.fnt");
    label->setScale(0.55f);
    auto* btn = CCMenuItemSpriteExtra::create(
        label, nullptr, this, menu_selector(IA_PauseLayer::onOpenPanel));
    btn->setPosition({win.width * 0.15f, win.height - 30.f});

    auto* menu = cocos2d::CCMenu::create();
    menu->addChild(btn);
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 100);
}

void onOpenPanel(cocos2d::CCObject*) {
    ia::AssistState::get().openPanel();
}
};

$on_mod(Loaded) {
ia::AssistState::get().initialize();
log::info("InputAssist cargado.");
}
