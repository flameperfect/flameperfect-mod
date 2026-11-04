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
//    "loading-color":        [r,g,b]         loading screen colour (when no image)
//    "loading-messages":     ["...", ...]    custom loading screen messages
//    "level-complete-flyin": true            LEVEL COMPLETE! flies into the end-screen popup
//    "chest-glow":           [r,g,b]         recolour the chest-opening glow + particles
//    "node-colors":          {"node-id":[r,g,b]}   recolour things other mods colour in code
//    "links":                {"facebook"|"twitter"|"youtube"|"twitch"|"discord"|"robtop": url}
//  Mod reminder: a pack can ship "fp_daily_clean.png"; its Daily button carries an
//  "install the Flameperfect mod" note, which this mod swaps for the clean image.
// =============================================================================
#include <Geode/Geode.hpp>
#include <algorithm>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

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

class $modify(FPMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto pj = fpPack();
        if (!pj) return true;
        auto j = *pj;

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

class $modify(FPLayer, CCLayer) {
    void onEnter() {
        CCLayer::onEnter();
        auto pj = fpPack();
        if (!pj) return;
        auto j = *pj;

        if (j.contains("node-colors")) {
            for (auto& item : j["node-colors"]) {
                auto key = item.getKey();
                if (!key) continue;
                auto node = this->getChildByIDRecursive(*key);
                if (!node) continue;
                auto rgba = typeinfo_cast<CCRGBAProtocol*>(node);
                auto col = fpColor(item);
                if (rgba && col) rgba->setColor(*col);
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
                break;
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
    if (!node || depth > 4) return nullptr;
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

class $modify(FPEndLevelLayer, EndLevelLayer) {
    void showLayer(bool instant) {
        EndLevelLayer::showLayer(instant);
        if (!s_completeText || !s_completeText->getParent()) return;
        auto title = fpFindComplete(this->m_mainLayer ? (CCNode*)this->m_mainLayer : (CCNode*)this);
        if (!title) return;
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        scene->addChild(FPFlyNode::create(s_completeText, title), 10000);
        s_completeText = nullptr;
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
