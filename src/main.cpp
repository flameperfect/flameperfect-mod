// =============================================================================
//  The Flame Engine  -  the mod behind my Flameperfect texture packs
//
//  Everything is switched on PER PACK by a file inside the applied texture pack:
//      flameperfect.json
//  No file (vanilla / someone else's pack) = the mod does nothing.
//
//  flameperfect.json keys (all optional):
//    "own-menu-icons":       true            title-screen icons use the player's own icons/colours
//    "menu-shader":          "file.glsl"     background shader for the main menu (static image or animated)
//    "menu-hide-ground":     true            hide the ground on the main menu (and on other menus with "all-menus")
//    "all-menus":            true            also draw that background on the other menus
//    "loading-background":   "file.png"      image behind the loading screen   (also: loading-bg.png)
//    "loading-shader":       "file.glsl"     animated loading screen (same format as menu-shader;
//                                            also gets "uniform float progress", 0..1 as GD loads)
//    "loading-color":        [r,g,b]         loading screen colour (when no image)
//    "loading-messages":     ["...", ...]    custom loading screen messages
//    "startup-intro":        "intro.webp"    animated webp played once over the main menu at launch
//    "startup-sound":        "intro.mp3"     sound played with it
//    "level-complete-flyin": true            LEVEL COMPLETE! flies into the end-screen popup
//    "chest-glow":           [r,g,b]         recolour the chest-opening glow + particles
//    "end-screen-bubbles":   "bubble.png"    bubbles rise through the end screen when it opens
//    "end-screen-color":     [r,g,b]         tint of the end-screen panel
//    "pause-color":          [r,g,b]         tint of the pause-menu panel
//    "pause-hide-background": true           no dark panel behind the pause menu
//    "ui-brown-to":          [r,g,b]         GD's brown list outlines / text boxes become this colour
//    "loading-sound-day" / "loading-sound-night": "a.ogg"   looping sounds that crossfade with loading progress
//    "tint-swaps":           [{"texture":"GJ_progressBar_001","from":[0,255,0],"to":[r,g,b]}]  swap a colour GD tints a picture with
//    "pause-bubbles":        true            bubbles drift up while paused (uses end-screen-bubbles' picture)
//    "debug-layout":         true            log menu layouts + where brown tints come from (for pack makers)
//    "end-screen-icons":     ["a.png"] or ["a.png","b.png"]  1 icon = attached in front of LEVEL COMPLETE, 2 = one each side
//    "node-colors":          {"node-id":[r,g,b]}   recolour nodes by node ID (search boxes, profile boxes, other mods)
//    "list-colors":          {"list-bg"|"list-cell-odd"|"list-cell-even"|"list-cell-selected"|"comment-list-bg"|
//                             "comment-cell-odd"|"comment-cell-even"|"comment-cell-box-odd"|"comment-cell-box-even"|
//                             "comment-cell-small-odd"|"comment-cell-small-even": [r,g,b]}   GD's brown lists
//    "background-color":     [r,g,b]         GD's blue menu background tint
//    "hide-nodes":           ["Layer/node-id", ...]   hide parts of a menu, e.g. "SecretRewardsLayer/floor"
//    "links":                {"facebook"|"twitter"|"youtube"|"twitch"|"discord"|"robtop": url}
//  Reminder note: a pack can draw a "Get The Flame Engine on Geode" note off its More Games
//  button, Daily button or logo, and ship fp_moregames_clean / fp_daily_clean / fp_logo_clean
//  pictures. With the mod installed, those clean pictures are swapped in so the note disappears.
// =============================================================================
#include <Geode/Geode.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>
#include <webp/demux.h>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// pack settings
static std::string fpFullPath(std::string const& name) {
    auto fu = CCFileUtils::sharedFileUtils();
    auto full = std::string(fu->fullPathForFilename(name.c_str(), false));
    if (full.empty() || !fu->isFileExist(full)) return "";
    return full;
}
// The applied pack's flameperfect.json. Cached, and only re-read when the file
// (or the applied pack) changes, so hooks that run often stay cheap.
static std::optional<matjson::Value> fpPack() {
    static std::string s_path;
    static std::filesystem::file_time_type s_time{};
    static std::optional<matjson::Value> s_json;

    auto full = fpFullPath("flameperfect.json");
    if (full.empty()) {
        s_path.clear();
        s_json = std::nullopt;
        return std::nullopt;
    }
    std::error_code ec;
    auto time = std::filesystem::last_write_time(std::filesystem::path(full), ec);
    if (full == s_path && !ec && time == s_time) return s_json;

    s_path = full;
    s_time = ec ? std::filesystem::file_time_type{} : time;
    auto res = utils::file::readJson(std::filesystem::path(full));
    if (!res) {
        log::warn("Flameperfect: couldn't read {}: {}", full, res.unwrapErr());
        s_json = std::nullopt;
    } else {
        s_json = res.unwrap();
    }
    return s_json;
}
static bool fpBool(matjson::Value const& j, char const* key) {
    return j.contains(key) && j[key].asBool().unwrapOr(false);
}
static std::string fpStr(matjson::Value const& j, char const* key) {
    return j.contains(key) ? j[key].asString().unwrapOr("") : "";
}
static std::optional<ccColor3B> fpColor(matjson::Value const& v) {
    if (!v.isArray() || v.size() < 3) return std::nullopt;
    auto c = [&](size_t i) { return (GLubyte)std::clamp<int64_t>(v[i].asInt().unwrapOr(255), 0, 255); };
    return ccColor3B{ c(0), c(1), c(2) };
}
static std::optional<ccColor3B> fpColorKey(matjson::Value const& j, char const* key) {
    if (!j.contains(key)) return std::nullopt;
    return fpColor(j[key]);
}

// ---------------------------------------------------------------------------
// Background node: runs a Menu-Shaders-style fragment shader full screen.
//   uniforms: vec2 resolution, float time, sampler2D sprite0..N ("//@a.png,b.png")
// --- loading ambience: a day loop and a night loop that crossfade with loading progress ---
// pack keys "loading-sound-day" / "loading-sound-night" (looping sounds)
static FMOD::Sound* s_ambSnd[2] = { nullptr, nullptr };
static FMOD::Channel* s_ambCh[2] = { nullptr, nullptr };
static float s_ambLevel[2] = { 0.f, 0.f };
static float s_bgTime = 0.f;      // shared shader clock, so the menu carries on from the loading screen
// loading -> menu handoff: if GD finishes loading before the loading scene reaches its end,
// the menu keeps showing the loading scene and finishes the change smoothly
static std::string s_loadShader;
static float s_loadEnd = 1.f;
static bool s_handoffActive = false;
static float fpSmooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
static void fpAmbStop() {
    for (int i = 0; i < 2; i++) {
        if (s_ambCh[i]) s_ambCh[i]->stop();
        if (s_ambSnd[i]) s_ambSnd[i]->release();
        s_ambCh[i] = nullptr; s_ambSnd[i] = nullptr;
    }
}
static void fpAmbApply(float scale) {
    auto fm = FMODAudioEngine::get();
    float v = fm ? fm->m_sfxVolume : 0.f;
    for (int i = 0; i < 2; i++) if (s_ambCh[i]) s_ambCh[i]->setVolume(v * s_ambLevel[i] * scale);
}
static void fpAmbStart(matjson::Value const& j) {
    fpAmbStop();
    auto fm = FMODAudioEngine::get();
    if (!fm || !fm->m_system || fm->m_sfxVolume <= 0.f) return;
    char const* keys[2] = { "loading-sound-day", "loading-sound-night" };
    for (int i = 0; i < 2; i++) {
        auto path = fpFullPath(fpStr(j, keys[i]));
        if (path.empty()) continue;
        if (fm->m_system->createSound(path.c_str(), FMOD_LOOP_NORMAL | FMOD_2D, nullptr, &s_ambSnd[i]) != FMOD_OK) { s_ambSnd[i] = nullptr; continue; }
        if (fm->m_system->playSound(s_ambSnd[i], nullptr, true, &s_ambCh[i]) != FMOD_OK) { s_ambCh[i] = nullptr; continue; }
        s_ambLevel[i] = i == 0 ? 1.f : 0.f;
    }
    fpAmbApply(1.f);
    for (int i = 0; i < 2; i++) if (s_ambCh[i]) s_ambCh[i]->setPaused(false);
}
// matches the loading shader: day fades out as the sky turns, night comes in for the last stretch
static void fpAmbProgress(float p) {
    s_ambLevel[0] = 1.f - fpSmooth(0.50f, 0.90f, p);
    s_ambLevel[1] = fpSmooth(0.60f, 0.95f, p);
    fpAmbApply(1.f);
}
// on the menu: the night sound carries on, then fades away
class FPAmbFade : public CCNode {
public:
    float m_t = 0.f, m_len = 7.f;
    static FPAmbFade* create() { auto r = new FPAmbFade(); r->init(); r->autorelease(); r->scheduleUpdate(); return r; }
    void update(float dt) override {
        if (s_handoffActive) return;          // the background is still finishing the day -> night change
        m_t += dt;
        float g = std::min(1.f, dt * 1.5f);
        s_ambLevel[0] += (0.f - s_ambLevel[0]) * g;
        s_ambLevel[1] += (1.f - s_ambLevel[1]) * g;
        float k = std::clamp(1.f - std::max(0.f, m_t - 2.0f) / (m_len - 2.0f), 0.f, 1.f);
        fpAmbApply(k * k);
        if (m_t >= m_len) { fpAmbStop(); this->removeFromParent(); }
    }
};

class FPBackground : public CCNode {
public:
    CCGLProgram* m_prog = nullptr;
    std::vector<Ref<CCTexture2D>> m_tex;
    std::vector<GLint> m_texLoc;
    GLint m_resLoc = -1, m_timeLoc = -1, m_progLoc = -1;
    float m_progress = 0.f;      // loading progress 0..1 (only moves on the loading screen)
    float m_progressTarget = 0.f;
    float m_time = 0.f;

    static FPBackground* create(std::string const& shaderFile) {
        auto ret = new FPBackground();
        if (ret->init(shaderFile)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    bool init(std::string const& shaderFile) {
        if (!CCNode::init()) return false;
        auto full = fpFullPath(shaderFile);
        if (full.empty()) return false;
        auto src = utils::file::readString(std::filesystem::path(full));
        if (!src) return false;
        std::string frag = src.unwrap();

        // sprites listed in "//@name.png,other.png" comments
        std::vector<std::string> sprites;
        std::istringstream lines(frag);
        for (std::string line; std::getline(lines, line);) {
            auto p = line.find("//@");
            if (p == std::string::npos) continue;
            std::stringstream names(line.substr(p + 3));
            for (std::string n; std::getline(names, n, ',');) {
                n.erase(0, n.find_first_not_of(" \t\r"));
                n.erase(n.find_last_not_of(" \t\r") + 1);
                if (!n.empty()) sprites.push_back(n);
            }
        }

        static char const* vert =
            "attribute vec4 a_position;\n"
            "void main() { gl_Position = a_position; }\n";
        m_prog = new CCGLProgram();
        if (!m_prog->initWithVertexShaderByteArray(vert, frag.c_str())) {
            log::error("Flameperfect: menu shader failed to compile ({})", shaderFile);
            m_prog->release(); m_prog = nullptr; return false;
        }
        m_prog->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        if (!m_prog->link()) {
            log::error("Flameperfect: menu shader failed to link ({})", shaderFile);
            m_prog->release(); m_prog = nullptr; return false;
        }
        m_prog->updateUniforms();
        GLuint p = m_prog->getProgram();
        m_resLoc = glGetUniformLocation(p, "resolution");
        m_timeLoc = glGetUniformLocation(p, "time");
        m_progLoc = glGetUniformLocation(p, "progress");
        for (size_t i = 0; i < sprites.size(); i++) {
            auto spr = CCSprite::create(sprites[i].c_str());
            if (!spr || !spr->getTexture()) continue;
            m_tex.push_back(spr->getTexture());
            m_texLoc.push_back(glGetUniformLocation(p, fmt::format("sprite{}", i).c_str()));
        }
        this->setContentSize(CCDirector::get()->getWinSize());
        this->setID("background"_spr);
        m_time = s_bgTime;
        this->scheduleUpdate();
        return true;
    }
    ~FPBackground() override { if (m_handoff) s_handoffActive = false; if (m_prog) m_prog->release(); }
    void update(float dt) override {
        m_time += dt;
        // ease towards the real progress so scenes change smoothly, not in jumps
        s_bgTime = m_time;
        if (m_handoff) {
            m_hand += dt;
            float len = std::max(2.0f, (1.f - m_handFrom) * 5.f);
            float k = fpSmooth(0.f, len, m_hand);
            m_progress = m_handFrom + (1.f - m_handFrom) * k;
            fpAmbProgress(m_progress);
            if (k >= 1.f) { m_handoff = false; s_handoffActive = false; }
            return;
        }
        // ease towards the real progress at a gentle pace, so the scene glides instead of jumping
        m_progress += (m_progressTarget - m_progress) * std::min(1.f, dt * 1.6f);
        if (m_isLoading) { fpAmbProgress(m_progress); s_loadEnd = m_progress; }
    }
    bool m_isLoading = false;
    bool m_handoff = false;
    float m_handFrom = 0.f, m_hand = 0.f;
    void draw() override {
        if (!m_prog) return;
        ccGLUseProgram(m_prog->getProgram());
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        if (m_resLoc >= 0) glUniform2f(m_resLoc, (float)vp[2], (float)vp[3]);
        if (m_timeLoc >= 0) glUniform1f(m_timeLoc, m_time);
        if (m_progLoc >= 0) glUniform1f(m_progLoc, m_progress);
        for (size_t i = 0; i < m_tex.size(); i++) {
            ccGLBindTexture2DN((GLuint)i, m_tex[i]->getName());
            if (m_texLoc[i] >= 0) glUniform1i(m_texLoc[i], (GLint)i);
        }
        ccGLBlendFunc(GL_ONE, GL_ZERO);
        static GLfloat const quad[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, quad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CC_INCREMENT_GL_DRAWS(1);
        ccGLBlendFunc(CC_BLEND_SRC, CC_BLEND_DST);
        ccGLBindTexture2DN(0, 0);
    }
};

// file name a texture was loaded from ("" if unknown)
static std::string fpTextureName(CCTexture2D* tex) {
    if (!tex) return "";
    auto dict = CCTextureCache::sharedTextureCache()->m_pTextures;
    if (!dict) return "";
    for (auto [key, obj] : CCDictionaryExt<std::string, CCTexture2D*>(dict)) {
        if (obj == tex) return key;
    }
    return "";
}

static void fpDumpTree(CCNode* root, char const* label);

// ---------------------------------------------------------------------------
// GD's brown UI tints: list outlines, text-box fills and other rounded boxes are white
// "square" / commentBorder pictures that GD tints brown in code, so a texture pack can't recolour them.
// With "ui-brown-to": [r,g,b] those brown tints become that colour (darker browns
// become a matching darker shade). Only touches the rounded-box pictures, never icons.
#include <Geode/modify/CCSprite.hpp>

static std::optional<ccColor3B> s_brownTo;
static bool s_debugLayout = false;
static std::unordered_map<CCTexture2D*, bool> s_squareTex;

// "tint-swaps": [{"texture": "GJ_progressBar_001", "from": [0,255,0], "to": [r,g,b]}]
// GD colours some white pictures in code (like the level progress bars); this swaps that colour.
struct FPTintSwap { std::string texture; ccColor3B from; ccColor3B to; };
static std::vector<FPTintSwap> s_tintSwaps;
static std::unordered_map<CCTexture2D*, std::string> s_texNames;

static void fpRefreshBrown() {
    auto pj = fpPack();
    s_brownTo = pj ? fpColorKey(*pj, "ui-brown-to") : std::nullopt;
    s_tintSwaps.clear();
    s_texNames.clear();
    if (pj && pj->contains("tint-swaps") && (*pj)["tint-swaps"].isArray()) {
        for (auto& item : (*pj)["tint-swaps"]) {
            auto from = fpColorKey(item, "from");
            auto to = fpColorKey(item, "to");
            if (!from || !to) continue;
            std::string tex = item.contains("texture") ? item["texture"].asString().unwrapOr("") : "";
            s_tintSwaps.push_back({ tex, *from, *to });
        }
    }
    s_debugLayout = pj && fpBool(*pj, "debug-layout");
}
static bool fpIsBrown(ccColor3B c) {
    return c.r > c.g && c.g > c.b && c.r - c.b >= 50 && c.r <= 230 && c.b <= 120;
}
static bool fpIsSquareTexture(CCTexture2D* tex) {
    if (!tex) return false;
    auto it = s_squareTex.find(tex);
    if (it != s_squareTex.end()) return it->second;
    auto name = fpTextureName(tex);
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    bool square = name.rfind("square0", 0) == 0 || name.rfind("GJ_square", 0) == 0
        || name.rfind("commentBorder", 0) == 0;
    s_squareTex[tex] = square;
    return square;
}

static bool fpTextureMatches(CCTexture2D* tex, std::string const& want) {
    if (want.empty()) return true;
    if (!tex) return false;
    auto it = s_texNames.find(tex);
    if (it == s_texNames.end()) it = s_texNames.emplace(tex, fpTextureName(tex)).first;
    return it->second.find(want) != std::string::npos;
}

class $modify(FPSprite, CCSprite) {
    void setColor(ccColor3B const& color) {
        for (auto& sw : s_tintSwaps) {
            if (color.r == sw.from.r && color.g == sw.from.g && color.b == sw.from.b
                && fpTextureMatches(this->getTexture(), sw.texture)) {
                CCSprite::setColor(sw.to);
                return;
            }
        }
        if (s_debugLayout && fpIsBrown(color)) {
            // pack-maker debugging: report where brown tints come from (once each)
            static std::set<std::string> seen;
            auto tex = fpTextureName(this->getTexture());
            auto slash = tex.find_last_of("/\\");
            if (slash != std::string::npos) tex = tex.substr(slash + 1);
            auto parent = this->getParent();
            auto key = fmt::format("{},{},{} {} {}", color.r, color.g, color.b, tex,
                parent ? std::string(getObjectName(parent)) : std::string("-"));
            if (seen.insert(key).second) log::info("Flameperfect brown tint: {} parent-id='{}'", key, parent ? parent->getID() : "");
        }
        if (s_brownTo && fpIsBrown(color) && fpIsSquareTexture(this->getTexture())) {
            auto lum = [](ccColor3B c) { return 0.30f * c.r + 0.59f * c.g + 0.11f * c.b; };
            float k = std::clamp(lum(color) / lum({ 130, 64, 32 }), 0.2f, 1.0f);
            auto ch = [&](GLubyte v) { return (GLubyte)std::clamp((int)(v * k + 0.5f), 0, 255); };
            CCSprite::setColor({ ch(s_brownTo->r), ch(s_brownTo->g), ch(s_brownTo->b) });
            return;
        }
        CCSprite::setColor(color);
    }
};

// ---------------------------------------------------------------------------
// Mod reminder on the logo: packs draw a "get the mod" banner off the GEOMETRY DASH
// logo (using the frame offset). With the mod installed, swap in the clean logo.
static CCSprite* fpFindSprite(CCNode* node, char const* frame, int depth = 0) {
    if (!node || depth > 6) return nullptr;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (auto spr = typeinfo_cast<CCSprite*>(child)) {
            if (cocos::isSpriteFrameName(spr, frame)) return spr;
        }
        if (auto found = fpFindSprite(child, frame, depth + 1)) return found;
    }
    return nullptr;
}
static void fpCleanLogo(CCNode* layer) {
    if (fpFullPath("fp_logo_clean.png").empty()) return;
    auto logo = fpFindSprite(layer, "GJ_logo_001.png");
    if (!logo) return;
    if (auto clean = CCSprite::create("fp_logo_clean.png")) {
        logo->setDisplayFrame(clean->displayFrame());
    }
}

// ---------------------------------------------------------------------------
// Loading screen
#include <Geode/modify/LoadingLayer.hpp>

static std::string s_loadingMsg;

class $modify(FPLoadingLayer, LoadingLayer) {
    bool init(bool refresh) {
        if (!LoadingLayer::init(refresh)) return false;
        s_squareTex.clear();
        fpRefreshBrown();
        fpCleanLogo(this);
        auto pj = fpPack();
        matjson::Value j = pj ? *pj : matjson::Value();

        CCSprite* oldBg = nullptr;   // GD's background = game_bg_01 tinted (0,102,255)
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
            auto spr = typeinfo_cast<CCSprite*>(child);
            if (!spr) continue;
            auto c = spr->getColor();
            if (c.r == 0 && c.g == 102 && c.b == 255) { oldBg = spr; break; }
        }
        int minZ = 0;
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) minZ = std::min(minZ, child->getZOrder());

        // animated loading screen: same kind of shader as the menu background
        std::string shader = pj ? fpStr(j, "loading-shader") : "";
        if (!shader.empty()) {
            if (auto bg = FPBackground::create(shader)) {
                bg->setID("loading-shader"_spr);
                bg->m_isLoading = true;
                s_loadShader = shader;
                s_loadEnd = 0.f;
                fpAmbStart(j);
                if (oldBg) oldBg->setVisible(false);
                this->addChild(bg, minZ - 1);
                return true;
            }
        }

        std::string image = pj ? fpStr(j, "loading-background") : "";
        if (image.empty() && !fpFullPath("loading-bg.png").empty()) image = "loading-bg.png";
        if (!image.empty() && !fpFullPath(image).empty()) {
            if (auto bg = CCSprite::create(image.c_str())) {
                auto win = CCDirector::get()->getWinSize();
                auto sz = bg->getContentSize();
                bg->setScale(std::max(win.width / sz.width, win.height / sz.height));
                bg->setPosition(win / 2);
                bg->setID("loading-bg"_spr);
                int z = 0;
                for (auto child : CCArrayExt<CCNode*>(this->getChildren())) z = std::min(z, child->getZOrder());
                if (oldBg) oldBg->setVisible(false);
                this->addChild(bg, z - 1);
            }
        } else if (pj && oldBg) {
            if (auto col = fpColorKey(j, "loading-color")) oldBg->setColor(*col);
        }
        return true;
    }
    void updateProgress(int progress) {
        LoadingLayer::updateProgress(progress);
        // shaders can read this as "uniform float progress" (0 at the start, 1 when loaded)
        if (auto bg = typeinfo_cast<FPBackground*>(this->getChildByID("loading-shader"_spr))) {
            bg->m_progressTarget = std::clamp(progress / 100.f, 0.f, 1.f);
        }
    }
    char const* getLoadingString() {
        auto pj = fpPack();
        if (pj && pj->contains("loading-messages")) {
            auto msgs = (*pj)["loading-messages"];
            if (msgs.isArray() && msgs.size() > 0) {
                s_loadingMsg = msgs[rand() % msgs.size()].asString().unwrapOr("");
                if (!s_loadingMsg.empty()) return s_loadingMsg.c_str();
            }
        }
        return LoadingLayer::getLoadingString();
    }
};

// ---------------------------------------------------------------------------
// Main menu: own icons, background, mod reminder, social links
#include <Geode/modify/MenuGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

static void fpApplyMyIcons(PlayerObject* p) {
    auto gm = GameManager::get();
    if (!p || !gm) return;
    int cube = gm->activeIconForType(IconType::Cube);
    if (p->m_isShip) {
        p->updatePlayerShipFrame(gm->activeIconForType(IconType::Ship));
        p->updatePlayerFrame(cube);
    } else if (p->m_isBall) {
        p->updatePlayerRollFrame(gm->activeIconForType(IconType::Ball));
    } else if (p->m_isBird) {
        p->updatePlayerBirdFrame(gm->activeIconForType(IconType::Ufo));
        p->updatePlayerFrame(cube);
    } else if (p->m_isDart) {
        p->updatePlayerDartFrame(gm->activeIconForType(IconType::Wave));
    } else if (p->m_isRobot) {
        p->updatePlayerRobotFrame(gm->activeIconForType(IconType::Robot));
    } else if (p->m_isSpider) {
        p->updatePlayerSpiderFrame(gm->activeIconForType(IconType::Spider));
    } else if (p->m_isSwing) {
        p->updatePlayerSwingFrame(gm->activeIconForType(IconType::Swing));
    } else {
        p->updatePlayerFrame(cube);
    }
    p->setColor(gm->colorForIdx(gm->getPlayerColor()));
    p->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
    if (gm->getPlayerGlow()) {
        p->enableCustomGlowColor(gm->colorForIdx(gm->getPlayerGlowColor()));
        p->updateGlowColor();
    }
}

class $modify(FPMenuGameLayer, MenuGameLayer) {
    void resetPlayer() {
        MenuGameLayer::resetPlayer();
        auto pj = fpPack();
        if (pj && fpBool(*pj, "own-menu-icons")) fpApplyMyIcons(m_playerObject);
    }
};

static CCMenuItemSpriteExtra* fpFindButton(CCNode* node, char const* frame, int depth = 0) {
    if (!node || depth > 6) return nullptr;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
            if (cocos::isSpriteFrameName(btn->getNormalImage(), frame)) return btn;
        }
        if (auto found = fpFindButton(child, frame, depth + 1)) return found;
    }
    return nullptr;
}

static bool fpOpenLink(char const* key) {
    auto pj = fpPack();
    if (!pj || !pj->contains("links")) return false;
    auto links = (*pj)["links"];
    if (!links.contains(key)) return false;
    auto url = links[key].asString();
    if (!url) return false;
    web::openLinkInBrowser(url.unwrap());
    return true;
}

// ---------------------------------------------------------------------------
// Updates: The Flame Engine isn't on the Geode index, so it checks GitHub itself.
// Once per launch (only with a Flameperfect pack applied) it looks at the latest
// release; if it's newer it asks, downloads the .geode into geode/mods, and the new
// version loads next time GD starts. "Later" asks again next launch.
static bool s_introRunning = false;
static std::optional<std::pair<std::string, std::string>> s_pendingUpdate;   // tag, download url
static std::optional<arc::TaskHandle<void>> s_updateTask;

static void fpDownloadUpdate(std::string tag, std::string url) {
    auto target = dirs::getModsDir() / "flameperfect.flameperfect.geode";
    s_updateTask = async::spawn(
        web::WebRequest().userAgent("flame-engine-updater").followRedirects(true).timeout(std::chrono::seconds(60)).get(url),
        [tag, target](web::WebResponse res) {
            std::error_code ec;
            auto tmp = target;
            tmp += ".download";
            if (!res.ok() || !res.into(tmp)) {
                FLAlertLayer::create("The Flame Engine", "Couldn't download the update. Try again next time you open GD.", "OK")->show();
                return;
            }
            std::filesystem::rename(tmp, target, ec);
            if (ec) {
                std::filesystem::remove(tmp, ec);
                FLAlertLayer::create("The Flame Engine", "Couldn't install the update. Try again next time you open GD.", "OK")->show();
                return;
            }
            FLAlertLayer::create("The Flame Engine", fmt::format("Updated to <cy>{}</c>! Restart GD to finish.", tag), "OK")->show();
        }
    );
}

static void fpShowPendingUpdate() {
    if (!s_pendingUpdate || s_introRunning) return;
    auto [tag, url] = *s_pendingUpdate;
    s_pendingUpdate.reset();
    createQuickPopup("The Flame Engine",
        fmt::format("A new version (<cy>{}</c>) is out.\nUpdate now?", tag),
        "Later", "Update",
        [tag, url](FLAlertLayer*, bool update) {
            if (update) fpDownloadUpdate(tag, url);
        });
}

static void fpCheckForUpdate() {
    static bool s_checked = false;
    if (s_checked || !Mod::get()->getSettingValue<bool>("check-updates")) return;
    s_checked = true;
    s_updateTask = async::spawn(
        web::WebRequest()
            .userAgent("flame-engine-updater")
            .header("Accept", "application/vnd.github+json")
            .timeout(std::chrono::seconds(10))
            .get("https://api.github.com/repos/flameperfect/flameperfect-mod/releases/latest"),
        [](web::WebResponse res) {
            if (!res.ok()) return;
            auto json = res.json();
            if (!json) return;
            auto tag = (*json)["tag_name"].asString().unwrapOr("");
            if (tag.empty()) return;
            auto latest = VersionInfo::parse(tag.starts_with("v") ? tag.substr(1) : tag);
            if (!latest || !(Mod::get()->getVersion() < latest.unwrap())) return;
            std::string url;
            if ((*json)["assets"].isArray()) {
                for (auto& a : (*json)["assets"]) {
                    if (a["name"].asString().unwrapOr("") == "flameperfect.flameperfect.geode") {
                        url = a["browser_download_url"].asString().unwrapOr("");
                    }
                }
            }
            if (url.empty()) return;
            s_pendingUpdate = std::make_pair(tag, url);
            fpShowPendingUpdate();
        }
    );
}

// ---------------------------------------------------------------------------
// Startup intro: plays the pack's animated .webp (+ optional sound) once, over the
// main menu, the first time it opens. Click to skip. Menu music waits until it ends.
class FPIntro : public CCLayer {
public:
    std::vector<uint8_t> m_data;
    WebPAnimDecoder* m_dec = nullptr;
    CCSprite* m_spr = nullptr;
    CCLayerColor* m_black = nullptr;
    int m_w = 0, m_h = 0;
    int m_nextTs = 0;           // ms at which the frame on screen ends
    float m_time = 0.f;
    bool m_started = false, m_done = false, m_pausedMusic = false;
    std::string m_soundPath;
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;

    static FPIntro* create(std::string const& webp, std::string const& sound) {
        auto ret = new FPIntro();
        if (ret->init(webp, sound)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    ~FPIntro() override {
        if (m_dec) WebPAnimDecoderDelete(m_dec);
        this->resumeMusic();
        if (m_sound) {
            bool playing = false;
            if (!m_channel || m_channel->isPlaying(&playing) != FMOD_OK || !playing) m_sound->release();
        }
    }
    bool init(std::string const& webp, std::string const& sound) {
        if (!CCLayer::init()) return false;
        auto bytes = utils::file::readBinary(std::filesystem::path(webp));
        if (!bytes) return false;
        m_data = bytes.unwrap();
        WebPData wd{ m_data.data(), m_data.size() };
        WebPAnimDecoderOptions opts;
        if (!WebPAnimDecoderOptionsInit(&opts)) return false;
        opts.color_mode = MODE_RGBA;
        opts.use_threads = 1;
        m_dec = WebPAnimDecoderNew(&wd, &opts);
        if (!m_dec) return false;
        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(m_dec, &info) || info.frame_count == 0) return false;
        m_w = (int)info.canvas_width;
        m_h = (int)info.canvas_height;
        uint8_t* buf = nullptr;
        if (!WebPAnimDecoderGetNext(m_dec, &buf, &m_nextTs)) return false;

        auto tex = new CCTexture2D();
        if (!tex->initWithData(buf, kCCTexture2DPixelFormat_RGBA8888, m_w, m_h, CCSize((float)m_w, (float)m_h))) {
            tex->release();
            return false;
        }
        tex->autorelease();

        auto win = CCDirector::get()->getWinSize();
        m_black = CCLayerColor::create({ 0, 0, 0, 255 });
        this->addChild(m_black);
        m_spr = CCSprite::createWithTexture(tex);
        auto sz = m_spr->getContentSize();
        m_spr->setScale(std::max(win.width / sz.width, win.height / sz.height));
        m_spr->setPosition(win / 2);
        this->addChild(m_spr);
        m_soundPath = sound;

        this->setID("startup-intro"_spr);
        this->setTouchEnabled(true);
        this->setTouchMode(kCCTouchesOneByOne);
        this->scheduleUpdate();
        return true;
    }
    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -1000, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override {
        this->finish(true);
        return true;
    }
    void resumeMusic() {
        if (!m_pausedMusic) return;
        m_pausedMusic = false;
        auto fm = FMODAudioEngine::get();
        if (fm && fm->m_backgroundMusicChannel) fm->m_backgroundMusicChannel->setPaused(false);
    }
    void start() {
        auto fm = FMODAudioEngine::get();
        if (!fm) return;
        if (fm->m_backgroundMusicChannel) {
            fm->m_backgroundMusicChannel->setPaused(true);
            m_pausedMusic = true;
        }
        if (!m_soundPath.empty() && fm->m_system && fm->m_sfxVolume > 0.f) {
            if (fm->m_system->createSound(m_soundPath.c_str(), FMOD_DEFAULT, nullptr, &m_sound) == FMOD_OK && m_sound) {
                if (fm->m_system->playSound(m_sound, nullptr, false, &m_channel) == FMOD_OK && m_channel) {
                    m_channel->setVolume(fm->m_sfxVolume);
                } else {
                    m_channel = nullptr;
                }
            }
        }
    }
    void update(float dt) override {
        if (m_done) return;
        // the first frame after loading is a long one: start the clock from the next
        if (!m_started) { m_started = true; this->start(); return; }
        m_time += std::min(dt, 0.1f);
        // keep the picture in sync with the sound while it plays
        if (m_channel) {
            bool playing = false;
            unsigned int ms = 0;
            if (m_channel->isPlaying(&playing) == FMOD_OK && playing
                && m_channel->getPosition(&ms, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                m_time = ms / 1000.f;
            }
        }
        int now = (int)(m_time * 1000.f);
        uint8_t* buf = nullptr;
        while (now >= m_nextTs && WebPAnimDecoderHasMoreFrames(m_dec)) {
            int ts = 0;
            if (!WebPAnimDecoderGetNext(m_dec, &buf, &ts)) break;
            m_nextTs = ts;
        }
        if (buf) {
            ccGLBindTexture2D(m_spr->getTexture()->getName());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
        }
        if (!WebPAnimDecoderHasMoreFrames(m_dec) && now >= m_nextTs) this->finish(false);
    }
    void finish(bool skipped) {
        if (m_done) return;
        m_done = true;
        this->unscheduleUpdate();
        this->setTouchEnabled(false);
        if (skipped && m_channel) m_channel->stop();
        this->resumeMusic();
        m_spr->runAction(CCFadeOut::create(0.35f));
        m_black->runAction(CCFadeOut::create(0.35f));
        s_introRunning = false;
        fpShowPendingUpdate();
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.4f),
            CCRemoveSelf::create(),
            nullptr));
    }
};

class $modify(FPMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        s_squareTex.clear();
        fpRefreshBrown();
        auto pj = fpPack();
        if (!pj) return true;
        auto j = *pj;

        // check GitHub for a newer version (once per launch)
        fpCheckForUpdate();

        // loading ambience: night sound carries over into the menu, then fades out
        if (s_ambCh[0] || s_ambCh[1]) {
            this->addChild(FPAmbFade::create());
        }

        // startup intro, once per launch
        static bool s_introPlayed = false;
        if (!s_introPlayed) {
            s_introPlayed = true;
            auto intro = fpFullPath(fpStr(j, "startup-intro"));
            if (!intro.empty() && Mod::get()->getSettingValue<bool>("play-intro")) {
                if (auto layer = FPIntro::create(intro, fpFullPath(fpStr(j, "startup-sound")))) {
                    this->addChild(layer, 1000);
                    s_introRunning = true;
                }
            }
        }

        // Mod reminder: packs draw a "get the mod" banner off one of these pictures
        // (using the frame offset, so it shows in the top-right corner). With the mod
        // installed, swap in the clean picture. setDisplayFrame also resets the offset.
        fpCleanLogo(this);
        auto cleanButton = [this](char const* frame, char const* cleanFile) {
            if (fpFullPath(cleanFile).empty()) return;
            auto btn = fpFindButton(this, frame);
            if (!btn) return;
            auto clean = CCSprite::create(cleanFile);
            auto spr = typeinfo_cast<CCSprite*>(btn->getNormalImage());
            if (!clean || !spr) return;
            spr->setDisplayFrame(clean->displayFrame());
            btn->setContentSize(spr->getScaledContentSize());
            spr->setPosition(btn->getContentSize() / 2);
        };
        cleanButton("GJ_moreGamesBtn_001.png", "fp_moregames_clean.png");
        cleanButton("GJ_dailyRewardBtn_001.png", "fp_daily_clean.png");

        // background
        auto shader = fpStr(j, "menu-shader");
        static bool s_handedOff = false;
        bool handoff = !s_handedOff && !s_loadShader.empty() && s_loadEnd < 0.995f;
        s_handedOff = true;
        if (!shader.empty() && m_menuGameLayer) {
            if (auto bg = FPBackground::create(handoff ? s_loadShader : shader)) {
                if (handoff) {
                    bg->m_handoff = true;
                    bg->m_handFrom = s_loadEnd;
                    bg->m_progress = s_loadEnd;
                    s_handoffActive = true;
                }
                auto mgl = m_menuGameLayer;
                int z = 0;
                for (auto child : CCArrayExt<CCNode*>(mgl->getChildren())) z = std::min(z, child->getZOrder());
                mgl->addChild(bg, z - 1);
                if (mgl->m_backgroundSprite) mgl->m_backgroundSprite->setVisible(false);
                if (fpBool(j, "menu-hide-ground") && mgl->m_groundLayer) mgl->m_groundLayer->setVisible(false);
                // if Menu Shaders is still installed it hides this layer - keep ours visible
                Ref<MenuLayer> self = this;
                Loader::get()->queueInMainThread([self]() {
                    if (self->m_menuGameLayer) self->m_menuGameLayer->setVisible(true);
                });
            }
        }
        return true;
    }
    void onFacebook(CCObject* s) { if (!fpOpenLink("facebook")) MenuLayer::onFacebook(s); }
    void onTwitter(CCObject* s)  { if (!fpOpenLink("twitter"))  MenuLayer::onTwitter(s); }
    void onYouTube(CCObject* s)  { if (!fpOpenLink("youtube"))  MenuLayer::onYouTube(s); }
    void onTwitch(CCObject* s)   { if (!fpOpenLink("twitch"))   MenuLayer::onTwitch(s); }
    void onDiscord(CCObject* s)  { if (!fpOpenLink("discord"))  MenuLayer::onDiscord(s); }
    void onRobTop(CCObject* s)   { if (!fpOpenLink("robtop"))   MenuLayer::onRobTop(s); }
};

// ---------------------------------------------------------------------------
// Other menus: pack background behind every menu that uses GJ_gradientBG,
// plus node recolours (e.g. Integrated Demonlist's brown search bar)
#include <Geode/modify/CCLayer.hpp>
#include <Geode/modify/CustomListView.hpp>
#include <Geode/modify/GJListLayer.hpp>
#include <Geode/modify/GJCommentListLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>

// "node-colors": recolour nodes by their node ID (other mods' things, search boxes, ...)
static void fpApplyNodeColors(CCNode* root) {
    auto pj = fpPack();
    if (!root || !pj || !pj->contains("node-colors")) return;
    for (auto& item : (*pj)["node-colors"]) {
        auto key = item.getKey();
        if (!key) continue;
        auto node = root->getChildByIDRecursive(*key);
        if (!node) continue;
        auto rgba = typeinfo_cast<CCRGBAProtocol*>(node);
        auto col = fpColor(item);
        if (rgba && col) rgba->setColor(*col);
    }
}

// "list-colors": GD's brown list panels and rows, in the pack's colours.
static std::optional<ccColor3B> fpListColor(char const* key) {
    auto pj = fpPack();
    if (!key || !pj || !pj->contains("list-colors")) return std::nullopt;
    auto lc = (*pj)["list-colors"];
    if (!lc.contains(key)) return std::nullopt;
    return fpColor(lc[key]);
}
static void fpColorCell(TableViewCell* cell) {
    if (!cell || !cell->m_backgroundLayer) return;
    auto c = cell->m_backgroundLayer->getColor();
    auto is = [&](int r, int g, int b) { return c.r == r && c.g == g && c.b == b; };
    bool comment = typeinfo_cast<CommentCell*>(cell) != nullptr;
    char const* row = nullptr;
    char const* box = nullptr;
    if (is(161, 88, 44))      { row = comment ? "comment-cell-odd" : "list-cell-odd";   box = comment ? "comment-cell-box-odd" : nullptr; }
    else if (is(194, 114, 62)) { row = comment ? "comment-cell-even" : "list-cell-even"; box = comment ? "comment-cell-box-even" : nullptr; }
    else if (comment && is(156, 85, 42)) row = "comment-cell-small-odd";
    else if (comment && is(144, 79, 39)) row = "comment-cell-small-even";
    else if (is(230, 150, 10)) row = "list-cell-selected";
    if (!row) return;
    if (auto col = fpListColor(row)) cell->m_backgroundLayer->setColor(*col);
    if (box) {
        if (auto col = fpListColor(box)) {
            if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(cell->getChildByIDRecursive("background"))) rgba->setColor(*col);
        }
    }
}

class $modify(FPCustomListView, CustomListView) {
    void setupList(float y) {
        CustomListView::setupList(y);
        if (!m_tableView || !m_tableView->m_cellArray) return;
        for (auto cell : CCArrayExt<TableViewCell*>(m_tableView->m_cellArray)) fpColorCell(cell);
    }
};

class $modify(FPGJListLayer, GJListLayer) {
    bool init(BoomListView* list, char const* title, ccColor4B color, float w, float h, int type) {
        if (!GJListLayer::init(list, title, color, w, h, type)) return false;
        auto c = this->getColor();
        if (c.r == 191 && c.g == 114 && c.b == 62) {
            if (auto col = fpListColor("list-bg")) this->setColor(*col);
        }
        return true;
    }
};

class $modify(FPGJCommentListLayer, GJCommentListLayer) {
    bool init(BoomListView* list, char const* title, ccColor4B color, float w, float h, bool blueBorder) {
        if (!GJCommentListLayer::init(list, title, color, w, h, blueBorder)) return false;
        auto c = this->getColor();
        if (c.r == 191 && c.g == 114 && c.b == 62) {
            if (auto col = fpListColor("comment-list-bg")) this->setColor(*col);
        }
        return true;
    }
};

// profiles fill in after they download, so colour them again once loaded
class $modify(FPProfilePage, ProfilePage) {
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        fpApplyNodeColors(this);
    }
};

class $modify(FPLayer, CCLayer) {
    void onEnter() {
        CCLayer::onEnter();
        auto pj = fpPack();
        if (!pj) return;
        auto j = *pj;

        fpApplyNodeColors(this);

        // "hide-nodes": ["SecretRewardsLayer/floor", ...] hides that node on that menu
        if (j.contains("hide-nodes") && j["hide-nodes"].isArray()) {
            std::string cls = std::string(getObjectName(this));
            if (auto p = cls.rfind("::"); p != std::string::npos) cls = cls.substr(p + 2);
            for (auto& item : j["hide-nodes"]) {
                auto entry = item.asString().unwrapOr("");
                auto slash = entry.find('/');
                if (slash == std::string::npos || entry.substr(0, slash) != cls) continue;
                if (auto node = this->getChildByIDRecursive(entry.substr(slash + 1))) node->setVisible(false);
            }
        }
        if (typeinfo_cast<CCScene*>(this->getParent()) && fpBool(j, "debug-layout")) {
            fpDumpTree(this, std::string(getObjectName(this)).c_str());
        }

        // GD's blue menu background tint (when the pack shader isn't drawn there)
        if (auto col = fpColorKey(j, "background-color")) {
            if (auto bg = typeinfo_cast<CCSprite*>(this->getChildByID("background"))) {
                auto c = bg->getColor();
                if (c.r == 0 && c.g == 102 && c.b == 255) bg->setColor(*col);
            }
        }

        if (fpBool(j, "all-menus") && !typeinfo_cast<MenuLayer*>(this) && !this->getChildByID("background"_spr)) {
            auto shader = fpStr(j, "menu-shader");
            if (shader.empty()) return;
            for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
                auto spr = typeinfo_cast<CCSprite*>(child);
                if (!spr || !cocos::isSpriteName(spr, "GJ_gradientBG.png")) continue;
                if (auto bg = FPBackground::create(shader)) {
                    this->addChild(bg, spr->getZOrder() - 1);
                    spr->setVisible(false);
                }
                fpDumpTree(this, std::string(getObjectName(this)).c_str());
                break;
            }
        }

        // other mods' menus (e.g. BetterInfo) draw their own full-screen picture instead of
        // GJ_gradientBG: replace the lowest full-screen sprite on top-level menu layers too
        if (fpBool(j, "all-menus") && !this->getChildByID("background"_spr)
            && typeinfo_cast<CCScene*>(this->getParent())
            && !typeinfo_cast<MenuLayer*>(this) && !typeinfo_cast<GJBaseGameLayer*>(this)
            && !typeinfo_cast<LoadingLayer*>(this) && !typeinfo_cast<FLAlertLayer*>(this)) {
            auto shader = fpStr(j, "menu-shader");
            auto win = CCDirector::get()->getWinSize();
            CCSprite* full = nullptr;
            for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
                auto spr = typeinfo_cast<CCSprite*>(child);
                if (!spr || !spr->isVisible()) continue;
                auto box = spr->boundingBox();
                if (box.size.width < win.width * 0.9f || box.size.height < win.height * 0.9f) continue;
                // only GD's own level backgrounds (game_bg_XX), so special art (shops, vaults) stays
                if (fpTextureName(spr->getTexture()).find("game_bg_") == std::string::npos) continue;
                if (!full || spr->getZOrder() < full->getZOrder()) full = spr;
            }
            if (full && !shader.empty()) {
                if (auto bg = FPBackground::create(shader)) {
                    this->addChild(bg, full->getZOrder() - 1);
                    full->setVisible(false);
                }
                fpDumpTree(this, std::string(getObjectName(this)).c_str());
            }
        }

        // menus that got the pack background: also hide their ground strip
        // (e.g. the coloured floor under the main levels), so the background shows to the bottom
        if (fpBool(j, "menu-hide-ground") && this->getChildByID("background"_spr)) {
            for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (typeinfo_cast<GJGroundLayer*>(child)) child->setVisible(false);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// LEVEL COMPLETE! flies into the end-screen popup
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

static CCSprite* fpFindComplete(CCNode* node, int depth = 0) {
    if (!node || depth > 7) return nullptr;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (auto spr = typeinfo_cast<CCSprite*>(child)) {
            if (cocos::isSpriteFrameName(spr, "GJ_levelComplete_001.png")) return spr;
        }
        if (auto found = fpFindComplete(child, depth + 1)) return found;
    }
    return nullptr;
}

static Ref<CCSprite> s_completeText = nullptr;

class $modify(FPPlayLayer, PlayLayer) {
    void showCompleteText() {
        s_completeText = nullptr;
        PlayLayer::showCompleteText();
        auto pj = fpPack();
        if (pj && fpBool(*pj, "level-complete-flyin")) s_completeText = fpFindComplete(this);
    }
    void resetLevel() { s_completeText = nullptr; PlayLayer::resetLevel(); }
    void onQuit() { s_completeText = nullptr; PlayLayer::onQuit(); }
};

static float fpWorldScale(CCNode* n) {
    float s = 1.f;
    for (auto p = n; p; p = p->getParent()) s *= p->getScale();
    return s;
}

class FPFlyNode : public CCNode {
public:
    Ref<CCSprite> m_fly;
    Ref<CCSprite> m_title;
    CCPoint m_startPos;
    float m_startScale = 1.f;
    float m_time = 0.f;
    float m_duration = 0.65f;

    static FPFlyNode* create(CCSprite* fly, CCSprite* title) {
        auto ret = new FPFlyNode();
        ret->m_fly = fly;
        ret->m_title = title;
        ret->init();
        ret->autorelease();
        return ret;
    }
    bool init() override {
        if (!CCNode::init()) return false;
        m_startPos = m_fly->getParent()->convertToWorldSpace(m_fly->getPosition());
        m_startScale = fpWorldScale(m_fly);
        m_fly->stopAllActions();
        m_fly->retain();
        m_fly->removeFromParentAndCleanup(false);
        m_fly->setPosition(m_startPos);
        m_fly->setScale(m_startScale);
        m_fly->setOpacity(255);
        m_fly->setVisible(true);
        this->addChild(m_fly);
        m_fly->release();
        m_title->setOpacity(0);
        this->scheduleUpdate();
        return true;
    }
    void update(float dt) override {
        m_time += dt;
        float t = std::min(m_time / m_duration, 1.f);
        float e = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
        if (!m_title || !m_title->getParent()) { this->removeFromParent(); return; }
        CCPoint target = m_title->getParent()->convertToWorldSpace(m_title->getPosition());
        float targetScale = fpWorldScale(m_title);
        m_fly->setPosition(m_startPos + (target - m_startPos) * e);
        m_fly->setScale(m_startScale + (targetScale - m_startScale) * e);
        if (t >= 1.f) {
            m_title->setOpacity(255);
            this->unscheduleUpdate();
            this->removeFromParent();
        }
    }
};

// custom icons either side of the LEVEL COMPLETE title (pack key "end-screen-icons")
static void fpAddEndIcons(CCNode* root) {
    auto pj = fpPack();
    if (!pj || !pj->contains("end-screen-icons")) return;
    auto list = (*pj)["end-screen-icons"];
    if (!list.isArray() || list.size() == 0) return;
    CCSprite* title = fpFindComplete(root);
    if (!title) title = typeinfo_cast<CCSprite*>(root->getChildByIDRecursive("level-complete-text"));
    if (!title) title = typeinfo_cast<CCSprite*>(root->getChildByIDRecursive("complete-message"));
    if (!title || !title->getParent()) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::warn("Flameperfect: end-screen-icons: LEVEL COMPLETE title not found. Children:");
            std::function<void(CCNode*, int)> dump = [&](CCNode* n, int d) {
                for (auto c : CCArrayExt<CCNode*>(n->getChildren())) {
                    auto spr = typeinfo_cast<CCSprite*>(c);
                    log::warn("{}{} id='{}' pos=({:.0f},{:.0f}) size=({:.0f},{:.0f}){}", std::string(d * 2, ' '),
                        getObjectName(c), c->getID(), c->getPositionX(), c->getPositionY(),
                        c->getScaledContentSize().width, c->getScaledContentSize().height,
                        spr ? " [sprite]" : "");
                    if (d < 3) dump(c, d + 1);
                }
            };
            dump(root, 0);
        }
        return;
    }
    auto parent = title->getParent();
    if (parent->getChildByID("end-icon-left"_spr)) return;

    auto titleSize = title->getScaledContentSize();
    auto pop = [](CCSprite* icon, float scale, float delay) {
        icon->setScale(0.f);
        icon->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCEaseBackOut::create(CCScaleTo::create(0.35f, scale)),
            nullptr
        ));
    };

    if (list.size() == 1) {
        // one icon: attached to the front of the title, same height as the text;
        // the title shifts right so the pair stays centred in the popup
        auto file = list[0].asString().unwrapOr("");
        if (file.empty() || fpFullPath(file).empty()) return;
        auto icon = CCSprite::create(file.c_str());
        if (!icon) return;
        float h = titleSize.height * 0.95f;
        float scale = h / std::max(1.f, icon->getContentSize().height);
        float w = icon->getContentSize().width * scale;
        float gap = 4.f;
        auto centre = title->getPosition();
        title->setPositionX(centre.x + (w + gap) / 2);
        icon->setPosition(ccp(centre.x - (titleSize.width + gap) / 2, centre.y));
        icon->setID("end-icon-left"_spr);
        parent->addChild(icon, title->getZOrder());
        pop(icon, scale, 0.15f);
        return;
    }

    // two icons: one either side of the title, bobbing gently
    for (int i = 0; i < 2; i++) {
        auto file = list[i].asString().unwrapOr("");
        if (file.empty() || fpFullPath(file).empty()) continue;
        auto icon = CCSprite::create(file.c_str());
        if (!icon) continue;
        float target = titleSize.height;
        float scale = target / std::max(1.f, icon->getContentSize().height);
        float side = i == 0 ? -1.f : 1.f;
        icon->setPosition(title->getPosition() + ccp(side * (titleSize.width / 2 + target / 2 + 6.f), 0));
        icon->setID(i == 0 ? "end-icon-left"_spr : "end-icon-right"_spr);
        parent->addChild(icon, title->getZOrder());
        pop(icon, scale, 0.15f + 0.1f * i);
        float dy = i == 0 ? 3.f : -3.f;
        icon->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCMoveBy::create(0.9f, ccp(0, dy))),
            CCEaseSineInOut::create(CCMoveBy::create(0.9f, ccp(0, -dy))),
            nullptr
        )));
    }
}

// log a layer's node tree once (used to plan pack designs for popups)
static void fpDumpTree(CCNode* root, char const* label) {
    static std::set<std::string> done;
    if (!root || done.count(label)) return;
    auto pj = fpPack();
    if (!pj || !fpBool(*pj, "debug-layout")) return;
    done.insert(label);
    log::info("Flameperfect layout dump: {}", label);
    std::function<void(CCNode*, int)> dump = [&](CCNode* n, int d) {
        for (auto c : CCArrayExt<CCNode*>(n->getChildren())) {
            auto spr = typeinfo_cast<CCSprite*>(c);
            auto tex = spr ? fpTextureName(spr->getTexture()) : std::string();
            auto slash = tex.find_last_of("/\\");
            if (slash != std::string::npos) tex = tex.substr(slash + 1);
            log::info("{}{} id='{}' z={} pos=({:.0f},{:.0f}) size=({:.0f},{:.0f}) col=({},{},{}) {}", std::string(d * 2, ' '),
                getObjectName(c), c->getID(), c->getZOrder(), c->getPositionX(), c->getPositionY(),
                c->getScaledContentSize().width, c->getScaledContentSize().height,
                typeinfo_cast<CCRGBAProtocol*>(c) ? (int)typeinfo_cast<CCRGBAProtocol*>(c)->getColor().r : -1,
                typeinfo_cast<CCRGBAProtocol*>(c) ? (int)typeinfo_cast<CCRGBAProtocol*>(c)->getColor().g : -1,
                typeinfo_cast<CCRGBAProtocol*>(c) ? (int)typeinfo_cast<CCRGBAProtocol*>(c)->getColor().b : -1,
                tex);
            if (d < 8) dump(c, d + 1);
        }
    };
    dump(root, 1);
}

// bubbles rising through the end-screen popup when it opens (pack key "end-screen-bubbles")
static void fpEndBubbles(CCNode* root) {
    auto pj = fpPack();
    if (!pj) return;
    auto file = fpStr(*pj, "end-screen-bubbles");
    if (file.empty() || fpFullPath(file).empty() || root->getChildByID("bubbles"_spr)) return;
    auto holder = CCNode::create();
    holder->setID("bubbles"_spr);
    root->addChild(holder, 100);
    auto win = CCDirector::get()->getWinSize();
    for (int i = 0; i < 26; i++) {
        auto b = CCSprite::create(file.c_str());
        if (!b) return;
        float r = (float)rand() / RAND_MAX;
        float scale = 0.25f + 0.55f * ((float)rand() / RAND_MAX);
        float x = win.width / 2 + (r - 0.5f) * 360.f;
        float y = win.height / 2 - 120.f - 20.f * ((float)rand() / RAND_MAX);
        float rise = 150.f + 130.f * ((float)rand() / RAND_MAX);
        float time = 1.3f + 1.0f * ((float)rand() / RAND_MAX);
        float delay = 0.25f + 0.6f * ((float)rand() / RAND_MAX);
        b->setPosition(ccp(x, y));
        b->setScale(scale);
        b->setOpacity(0);
        holder->addChild(b);
        float wob = (rand() % 2 ? 1.f : -1.f) * (6.f + 6.f * r);
        b->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCSpawn::create(
                CCFadeTo::create(0.15f, 220),
                CCEaseSineOut::create(CCMoveBy::create(time, ccp(0, rise))),
                CCSequence::create(CCMoveBy::create(time / 3, ccp(wob, 0)), CCMoveBy::create(time / 3, ccp(-2 * wob, 0)), CCMoveBy::create(time / 3, ccp(wob, 0)), nullptr),
                CCSequence::create(CCDelayTime::create(time * 0.6f), CCFadeOut::create(time * 0.4f), nullptr),
                nullptr
            ),
            CCRemoveSelf::create(),
            nullptr
        ));
    }
}

// tint a popup's dark panel (keeps its see-through opacity)
static void fpTint(CCNode* root, char const* id, char const* key) {
    auto pj = fpPack();
    if (!pj || !root) return;
    auto col = fpColorKey(*pj, key);
    if (!col) return;
    if (auto node = typeinfo_cast<CCRGBAProtocol*>(root->getChildByIDRecursive(id))) node->setColor(*col);
}

// a slow, endless stream of bubbles (pause menu)
static void fpBubbleStream(CCNode* root, std::string const& file) {
    if (file.empty() || fpFullPath(file).empty() || root->getChildByID("bubble-stream"_spr)) return;
    auto holder = CCNode::create();
    holder->setID("bubble-stream"_spr);
    root->addChild(holder, -1);
    auto win = CCDirector::get()->getWinSize();
    auto spawn = [holder, file, win]() {
        auto b = CCSprite::create(file.c_str());
        if (!b) return;
        float r = (float)rand() / RAND_MAX;
        float scale = 0.25f + 0.45f * ((float)rand() / RAND_MAX);
        float time = 5.f + 4.f * ((float)rand() / RAND_MAX);
        b->setPosition(ccp(win.width * (0.08f + 0.84f * r), -10.f));
        b->setScale(scale);
        b->setOpacity(0);
        holder->addChild(b);
        float wob = (rand() % 2 ? 1.f : -1.f) * (5.f + 8.f * r);
        b->runAction(CCSpawn::create(
            CCFadeTo::create(0.5f, 150),
            CCMoveBy::create(time, ccp(0, win.height + 20.f)),
            CCRepeat::create(CCSequence::create(CCMoveBy::create(0.8f, ccp(wob, 0)), CCMoveBy::create(0.8f, ccp(-wob, 0)), nullptr), (unsigned)(time / 1.6f)),
            nullptr
        ));
        b->runAction(CCSequence::create(CCDelayTime::create(time), CCRemoveSelf::create(), nullptr));
    };
    holder->runAction(CCRepeatForever::create(CCSequence::create(
        CallFuncExt::create(spawn), CCDelayTime::create(0.45f), nullptr
    )));
}

class $modify(FPEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        // the popup is fully built after customSetup; add the icons on the next frame
        Ref<EndLevelLayer> self = this;
        Loader::get()->queueInMainThread([self]() {
            fpAddEndIcons(self->m_mainLayer ? (CCNode*)self->m_mainLayer : (CCNode*)self.data());
            fpTint(self.data(), "background", "end-screen-color");
            fpDumpTree(self.data(), "EndLevelLayer");
        });
    }
    void showLayer(bool instant) {
        EndLevelLayer::showLayer(instant);
        fpAddEndIcons(this->m_mainLayer ? (CCNode*)this->m_mainLayer : (CCNode*)this);
        fpEndBubbles(this);
        if (!s_completeText || !s_completeText->getParent()) return;
        auto title = fpFindComplete(this->m_mainLayer ? (CCNode*)this->m_mainLayer : (CCNode*)this);
        if (!title) return;
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        scene->addChild(FPFlyNode::create(s_completeText, title), 10000);
        s_completeText = nullptr;
    }
};

#include <Geode/modify/PauseLayer.hpp>
class $modify(FPPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        Ref<PauseLayer> self = this;
        Loader::get()->queueInMainThread([self]() {
            fpTint(self.data(), "background", "pause-color");
            if (auto pj = fpPack()) {
                if (fpBool(*pj, "pause-hide-background")) {
                    if (auto bg = self->getChildByID("background")) bg->setVisible(false);
                }
            }
            if (auto pj = fpPack()) {
                if (fpBool(*pj, "pause-bubbles")) fpBubbleStream(self.data(), fpStr(*pj, "end-screen-bubbles"));
            }
            fpDumpTree(self.data(), "PauseLayer");
        });
    }
};

// ---------------------------------------------------------------------------
// Chest opening: recolour GD's blue glow / light / particles to the pack colour
#include <Geode/modify/RewardUnlockLayer.hpp>

static ccColor3B s_glow = { 255, 120, 20 };
static bool fpIsBlue(ccColor3B c) { return c.b > c.r + 50 && c.b >= c.g; }
static bool fpIsBlue4(ccColor4F c) { return c.b > c.r + 0.2f && c.b >= c.g; }
static ccColor3B fpTint(ccColor3B c) {
    float v = std::max({ c.r, c.g, c.b }) / 255.f;
    return { (GLubyte)std::min(255.f, s_glow.r * v + 20 * v), (GLubyte)(s_glow.g * v), (GLubyte)(s_glow.b * v) };
}
static ccColor4F fpTint4(ccColor4F c) {
    float v = std::max({ c.r, c.g, c.b });
    return { std::min(1.f, s_glow.r / 255.f * v + 0.08f), s_glow.g / 255.f * v, s_glow.b / 255.f * v, c.a };
}
static void fpRecolor(CCNode* node, int depth = 0) {
    if (!node || depth > 8) return;
    if (auto ps = typeinfo_cast<CCParticleSystem*>(node)) {
        if (fpIsBlue4(ps->getStartColor())) ps->setStartColor(fpTint4(ps->getStartColor()));
        if (fpIsBlue4(ps->getEndColor())) ps->setEndColor(fpTint4(ps->getEndColor()));
    } else if (auto spr = typeinfo_cast<CCSprite*>(node)) {
        if (fpIsBlue(spr->getColor())) spr->setColor(fpTint(spr->getColor()));
    }
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) fpRecolor(child, depth + 1);
}

class $modify(FPRewardUnlockLayer, RewardUnlockLayer) {
    struct Fields { float m_left = 0.f; };
    void startRecolor() {
        auto pj = fpPack();
        if (!pj) return;
        auto col = fpColorKey(*pj, "chest-glow");
        if (!col) return;
        s_glow = *col;
        m_fields->m_left = 6.f;
        this->schedule(schedule_selector(FPRewardUnlockLayer::recolorTick), 0.f);
    }
    void recolorTick(float dt) {
        fpRecolor(this);
        m_fields->m_left -= dt;
        if (m_fields->m_left <= 0.f) this->unschedule(schedule_selector(FPRewardUnlockLayer::recolorTick));
    }
    void step2() { RewardUnlockLayer::step2(); startRecolor(); }
    void step3() { RewardUnlockLayer::step3(); startRecolor(); }
    void playRewardEffect() { RewardUnlockLayer::playRewardEffect(); startRecolor(); }
};
