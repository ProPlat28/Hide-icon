#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <vector>

using namespace geode::prelude;

struct PlayerSnapshot {
    cocos2d::CCPoint position;
};

struct GameSnapshot {
    float time = 0.f;

    PlayerSnapshot player1;
    PlayerSnapshot player2;
};

class RewindTimeline {
private:
    std::vector<GameSnapshot> m_frames;
    size_t m_current = 0;

public:
    void clear() {
        m_frames.clear();
        m_current = 0;
    }

    bool empty() const {
        return m_frames.empty();
    }

    size_t size() const {
        return m_frames.size();
    }

    void push(GameSnapshot const& frame) {
        m_frames.push_back(frame);
        m_current = m_frames.size() - 1;
    }

    GameSnapshot const* get(size_t index) const {
        if (index >= m_frames.size()) {
            return nullptr;
        }

        return &m_frames[index];
    }

    size_t current() const {
        return m_current;
    }

    void setCurrent(size_t index) {
        if (m_frames.empty()) {
            m_current = 0;
            return;
        }

        m_current = std::min(
            index,
            m_frames.size() - 1
        );
    }

    void deleteFuture() {
        if (m_frames.empty()) {
            return;
        }

        if (m_current + 1 >= m_frames.size()) {
            return;
        }

        m_frames.erase(
            m_frames.begin() + m_current + 1,
            m_frames.end()
        );
    }
};

class RewindManager {
private:
    RewindTimeline m_timeline;

    bool m_holding = false;
    bool m_rewinding = false;

    float m_holdTimer = 0.f;
    float m_recordTimer = 0.f;
    float m_rewindTimer = 0.f;

    static constexpr float HOLD_TIME = 2.f;
    static constexpr float RECORD_FPS = 30.f;
    static constexpr float REWIND_SPEED = 4.f;

public:
    static RewindManager& get() {
        static RewindManager instance;
        return instance;
    }

    void reset() {
        m_timeline.clear();

        m_holding = false;
        m_rewinding = false;

        m_holdTimer = 0.f;
        m_recordTimer = 0.f;
        m_rewindTimer = 0.f;
    }

    void onKey(bool down) {
        auto* layer = PlayLayer::get();

        if (!layer) {
            return;
        }

        if (down) {
            if (!m_holding && !m_rewinding) {
                m_holding = true;
                m_holdTimer = 0.f;
            }

            return;
        }

        m_holding = false;

        if (m_rewinding) {
            stopRewind(layer);
        }
    }

    void update(
        float dt,
        PlayLayer* layer
    ) {
        if (!layer) {
            return;
        }

        if (m_holding && !m_rewinding) {
            m_holdTimer += dt;

            if (m_holdTimer >= HOLD_TIME) {
                startRewind();
            }
        }

        if (m_rewinding) {
            updateRewind(
                dt,
                layer
            );

            return;
        }

        if (!m_holding) {
            m_recordTimer += dt;

            constexpr float frameTime =
                1.f / RECORD_FPS;

            while (m_recordTimer >= frameTime) {
                m_recordTimer -= frameTime;
                captureFrame(layer);
            }
        }
    }

private:
    void startRewind() {
        if (m_timeline.empty()) {
            return;
        }

        m_rewinding = true;
        m_rewindTimer = 0.f;

        m_timeline.setCurrent(
            m_timeline.size() - 1
        );

        log::info("Rewind started");
    }

    void updateRewind(
        float dt,
        PlayLayer* layer
    ) {
        if (m_timeline.empty()) {
            m_rewinding = false;
            return;
        }

        size_t current =
            m_timeline.current();

        if (current == 0) {
            restoreFrame(
                layer,
                m_timeline.get(0)
            );

            return;
        }

        m_rewindTimer += dt;

        float frames =
            m_rewindTimer *
            RECORD_FPS *
            REWIND_SPEED;

        if (frames < 1.f) {
            return;
        }

        size_t amount =
            static_cast<size_t>(frames);

        m_rewindTimer = 0.f;

        if (amount >= current) {
            current = 0;
        }
        else {
            current -= amount;
        }

        m_timeline.setCurrent(current);

        restoreFrame(
            layer,
            m_timeline.get(current)
        );
    }

    void captureFrame(
        PlayLayer* layer
    ) {
        GameSnapshot snapshot;

        snapshot.time =
            static_cast<float>(
                m_timeline.size()
            ) / RECORD_FPS;

        if (auto* player = layer->m_player1) {
            snapshot.player1.position =
                player->getPosition();
        }

        if (auto* player = layer->m_player2) {
            snapshot.player2.position =
                player->getPosition();
        }

        m_timeline.push(snapshot);
    }

    void restoreFrame(
        PlayLayer* layer,
        GameSnapshot const* snapshot
    ) {
        if (!snapshot) {
            return;
        }

        if (auto* player = layer->m_player1) {
            player->setPosition(
                snapshot->player1.position
            );
        }

        if (auto* player = layer->m_player2) {
            player->setPosition(
                snapshot->player2.position
            );
        }
    }

    void stopRewind(
        PlayLayer* layer
    ) {
        if (!m_rewinding) {
            return;
        }

        m_rewinding = false;

        m_timeline.deleteFuture();

        if (!m_timeline.empty()) {
            restoreFrame(
                layer,
                m_timeline.get(
                    m_timeline.current()
                )
            );
        }

        m_holdTimer = 0.f;
        m_rewindTimer = 0.f;

        log::info(
            "Rewind finished"
        );
    }
};

$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "rewind-key",
        [](
            Keybind const&,
            bool down,
            bool repeat,
            double
        ) {
            if (repeat) {
                return;
            }

            RewindManager::get()
                .onKey(down);
        }
    );
}

class $modify(
    RewindPlayLayer,
    PlayLayer
) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        RewindManager::get()
            .update(
                dt,
                this
            );
    }

    void onQuit() {
        RewindManager::get().reset();

        PlayLayer::onQuit();
    }
};