// =============================================================================
//  Flameperfect  -  the mod that powers Flameperfect texture packs
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
//    "loading-shader":       "file.glsl"     animated loading screen (same format as menu-shader)
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
//    "pause-bubbles":        true            bubbles drift up while paused (uses end-screen-bubbles' picture)
//    "debug-layout":         true            log the end-screen / pause-menu layout (for pack makers)
//    "end-screen-icons":     ["a.png"] or ["a.png","b.png"]  1 icon = attached in front of LEVEL COMPLETE, 2 = one each side
//    "node-colors":          {"node-id":[r,g,b]}   recolour nodes by node ID (search boxes, profile boxes, other mods)
//    "list-colors":          {"list-bg"|"list-cell-odd"|"list-cell-even"|"list-cell-selected"|"comment-list-bg"|
//                             "comment-cell-odd"|"comment-cell-even"|"comment-cell-box-odd"|"comment-cell-box-even"|
//                             "comment-cell-small-odd"|"comment-cell-small-even": [r,g,b]}   GD's brown lists
//    "background-color":     [r,g,b]         GD's blue menu background tint
//    "menu-transition":      "bubble.png"    a wave of these sweeps up the screen when changing menus
//    "menu-transition-color": [r,g,b]        colour the menu fade goes through (instead of black)
//    "links":                {"facebook"|"twitter"|"youtube"|"twitch"|"discord"|"robtop": url}
//  Mod reminder: a pack can ship "fp_daily_clean.png"; its Daily button carries an
//  "install the Flameperfect mod" note, which this mod swaps for the clean image.
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
class FPBackground : public CCNode {
public:
    CCGLProgram* m_prog = nullptr;
    std::vector<Ref<CCTexture2D>> m_tex;
    std::vector<GLint> m_texLoc;
    GLint m_resLoc = -1, m_timeLoc = -1;
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
        for (size_t i = 0; i < sprites.size(); i++) {
            auto spr = CCSprite::create(sprites[i].c_str());
            if (!spr || !spr->getTexture()) continue;
            m_tex.push_back(spr->getTexture());
            m_texLoc.push_back(glGetUniformLocation(p, fmt::format("sprite{}", i).c_str()));
        }
        this->setContentSize(CCDirector::get()->getWinSize());
        this->setID("background"_spr);
        this->scheduleUpdate();
        return true;
    }
    ~FPBackground() override { if (m_prog) m_prog->release(); }
    void update(float dt) override { m_time += dt; }
    void draw() override {
        if (!m_prog) return;
        ccGLUseProgram(m_prog->getProgram());
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        if (m_resLoc >= 0) glUniform2f(m_resLoc, (float)vp[2], (float)vp[3]);
        if (m_timeLoc >= 0) glUniform1f(m_timeLoc, m_time);
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

static void fpRefreshBrown() {
    auto pj = fpPack();
    s_brownTo = pj ? fpColorKey(*pj, "ui-brown-to") : std::nullopt;
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

class $modify(FPSprite, CCSprite) {
    void setColor(ccColor3B const& color) {
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

        // startup intro, once per launch
        static bool s_introPlayed = false;
        if (!s_introPlayed) {
            s_introPlayed = true;
            auto intro = fpFullPath(fpStr(j, "startup-intro"));
            if (!intro.empty() && Mod::get()->getSettingValue<bool>("play-intro")) {
                if (auto layer = FPIntro::create(intro, fpFullPath(fpStr(j, "startup-sound")))) {
                    this->addChild(layer, 1000);
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
        if (!shader.empty() && m_menuGameLayer) {
            if (auto bg = FPBackground::create(shader)) {
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

// ---------------------------------------------------------------------------
// Menu transitions ("menu-transition": "bubble.png", "menu-transition-color": [r,g,b]):
// GD's fade between menus goes through the pack's colour while a wave of bubbles
// sweeps up the screen, covering the swap at the halfway point.
#include <Geode/modify/CCTransitionFade.hpp>

class $modify(FPTransitionFade, CCTransitionFade) {
    void onEnter() {
        auto pj = fpPack();
        if (pj) {
            if (auto col = fpColorKey(*pj, "menu-transition-color")) {
                m_tColor = ccc4(col->r, col->g, col->b, 255);
            }
        }
        CCTransitionFade::onEnter();
        if (!pj) return;
        auto file = fpStr(*pj, "menu-transition");
        if (file.empty() || fpFullPath(file).empty()) return;

        auto holder = CCNode::create();
        holder->setID("transition-bubbles"_spr);
        this->addChild(holder, 1000);
        auto win = CCDirector::get()->getWinSize();
        float d = std::max(0.2f, m_fDuration);
        auto rnd = []() { return (float)rand() / RAND_MAX; };
        for (int i = 0; i < 60; i++) {
            auto b = CCSprite::create(file.c_str());
            if (!b) return;
            float size = std::max(1.f, b->getContentSize().width);
            float scale = (14.f + 46.f * rnd() * rnd()) / size * (win.height / 320.f);
            float x = win.width * (-0.05f + 1.1f * rnd());
            // bubbles bunch up around the middle of the fade, so the swap happens behind them
            float start = d * (0.05f + 0.35f * rnd());
            float time = d * (0.55f + 0.35f * rnd());
            float half = size * scale / 2;
            b->setScale(scale);
            b->setPosition(ccp(x, -half));
            b->setOpacity(0);
            holder->addChild(b);
            float wob = (rnd() < 0.5f ? 1.f : -1.f) * (4.f + 10.f * rnd());
            b->runAction(CCSequence::create(
                CCDelayTime::create(start),
                CCSpawn::create(
                    CCFadeTo::create(time * 0.15f, 235),
                    CCEaseSineIn::create(CCMoveBy::create(time, ccp(wob, win.height + 2 * half))),
                    CCSequence::create(CCDelayTime::create(time * 0.75f), CCFadeOut::create(time * 0.25f), nullptr),
                    nullptr
                ),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
    }
};

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
