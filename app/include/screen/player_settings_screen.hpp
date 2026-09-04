#pragma once

#include <borealis.hpp>

// Audio/subtitle track, playback speed and subtitle sync -- pushed as its
// own screen from PlayerScreen (START), the same way StreamNX-main's
// PlayerSetting is its own Activity rather than a panel drawn on top of the
// video. B pops back to the player.
class PlayerSettingsScreen : public brls::Box
{
  public:
    PlayerSettingsScreen();

  private:
    void refreshLabels();
    void cycleTrack(const std::string& type);
    void adjustSpeed(double delta);
    void adjustSubDelay(double delta);

    brls::Label* audioValue    = nullptr;
    brls::Label* subValue      = nullptr;
    brls::Label* speedValue    = nullptr;
    brls::Label* subDelayValue = nullptr;
};
