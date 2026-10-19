// Quand écrire les rapports de session : décision commune à la layer OpenXR et à XRPerfVR.
//   automatique : un rapport par session, du début à la fin du jeu ;
//   manuel      : un rapport par enregistrement, démarré et arrêté par le même raccourci (Ctrl+Alt+S) ;
//   aucun       : rien n'est écrit (les mesures du panneau continuent).
// Code portable (l'état du clavier est fourni par l'appelant), testé dans tests/preview.cpp.
#pragma once

#include "config.h"

namespace xrpo {

constexpr double kReportMinSeconds = 15.0;        // automatique : une session plus courte ne laisse pas de rapport
constexpr double kManualReportMinSeconds = 5.0;   // manuel : un enregistrement plus court est abandonné
constexpr double kReportPeriod = 15.0;            // intervalle entre deux réécritures du rapport en cours
constexpr double kRecordNoticeSeconds = 4.0;      // durée du témoin « rapport écrit » après l'arrêt

// Témoin d'enregistrement affiché sur le panneau (PanelData::recordState).
enum RecordIndicator : int {
    kRecordNone = 0,      // rien à afficher (mode automatique ou aucun, ou manuel au repos)
    kRecordRunning = 1,   // enregistrement manuel en cours
    kRecordSaved = 2,     // enregistrement arrêté, rapport écrit
    kRecordTooShort = 3,  // enregistrement arrêté trop tôt : pas de rapport
};

class ReportControl {
  public:
    struct Step {
        bool stop = false;    // terminer l'enregistrement en cours (rapport écrit s'il est assez long)
        bool start = false;   // commencer un enregistrement (après l'arrêt, s'il y en a un)
        bool manual = false;  // l'enregistrement commencé est manuel
    };

    // Début d'une session de jeu. Retourne true si un enregistrement automatique commence.
    // `keyDown` : état du raccourci à cet instant (une touche déjà enfoncée ne déclenche rien).
    bool begin(ReportMode mode, bool keyDown) {
        m_mode = mode;
        m_keyWasDown = keyDown;
        m_noticeUntil = -1.0;
        m_notice = kRecordNone;
        return mode == ReportMode::Auto;
    }

    // À chaque image (ou plus souvent). `mode` : réglage en cours, qui peut changer en jeu depuis
    // XRPerfSensors ; `recording` : un enregistrement est en cours ; `keyDown` : Ctrl+Alt+touche enfoncés.
    Step update(ReportMode mode, bool recording, bool keyDown) {
        Step s;
        const bool pressed = keyDown && !m_keyWasDown;
        m_keyWasDown = keyDown;
        if (mode != m_mode) {
            // Mode changé : l'enregistrement en cours se termine ; en automatique, un nouveau commence
            // tout de suite (le rapport couvre alors la suite de la session).
            m_mode = mode;
            s.stop = recording;
            s.start = mode == ReportMode::Auto;
            return s;
        }
        if (mode == ReportMode::Manual && pressed) {
            if (recording) {
                s.stop = true;
            } else {
                s.start = true;
                s.manual = true;
            }
        }
        return s;
    }

    // Enregistrement manuel terminé : `kept` = rapport écrit (sinon trop court), `seconds` = sa durée.
    void stoppedManual(double now, bool kept, double seconds) {
        m_notice = kept ? kRecordSaved : kRecordTooShort;
        m_noticeUntil = now + kRecordNoticeSeconds;
        m_noticeSeconds = seconds;
    }

    // Témoin à afficher, et la durée qui l'accompagne : celle de l'enregistrement en cours
    // (`manualRecording`, commencé à `recordStart`), ou celle de l'enregistrement qui vient de finir.
    int indicator(double now, bool manualRecording, double recordStart, double& seconds) const {
        seconds = 0.0;
        if (manualRecording) {
            seconds = now - recordStart;
            return kRecordRunning;
        }
        if (m_notice != kRecordNone && now < m_noticeUntil) {
            seconds = m_noticeSeconds;
            return m_notice;
        }
        return kRecordNone;
    }

  private:
    ReportMode m_mode = ReportMode::Auto;
    bool m_keyWasDown = false;
    int m_notice = kRecordNone;
    double m_noticeUntil = -1.0;
    double m_noticeSeconds = 0.0;
};

// Rapport à garder à la fin d'un enregistrement : automatique, au moins kReportMinSeconds d'images
// mesurées ; manuel, au moins kManualReportMinSeconds de durée (pause comprise) avec des images.
inline bool KeepReport(bool manual, double activeSeconds, double wallSeconds) {
    return manual ? wallSeconds >= kManualReportMinSeconds && activeSeconds > 0.0 : activeSeconds >= kReportMinSeconds;
}

}  // namespace xrpo
