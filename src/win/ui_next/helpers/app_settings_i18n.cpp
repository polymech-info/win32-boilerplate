#include "helpers/app_settings_i18n.hpp"

#include <string>

namespace pmui::app_settings_i18n {
namespace {

static std::string norm(std::string_view v)
{
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static const Strings kEn{
    L"App Settings",

    L"Language:",
    {L"English", L"Español", L"Deutsch", L"Italiano", L"Français"},

    L"Theme:",
    L"System (follow Windows)",
    L"Light",
    L"Dark",

    L"Font size:",
    {L"Default (system size)", L"+1 pt", L"+2 pt  (recommended)", L"+3 pt", L"+4 pt"},

    L"Theme and font apply right away. Language may need a restart.",

    L"AI Provider Keys…",
    L"Chat Provider Keys…",
    L"Export settings…",
    L"Import settings…",

    L"Encrypt export (PME1 — this Windows profile)",

    L"Explorer panel: full Windows Explorer UI. May stay light in dark theme.",

    L"Machine ID (for license activation):",

    L"Copy",

    L"Save",
    L"Cancel",

    L"Export failed:\n",
    L"Settings exported.",
    L"Import will replace your current settings.\n\nContinue?",
    L"Import failed:\n",
    L"Settings imported.\n\n"
    L"If anything looks unchanged, restart the app.",

    L"Fingerprint copied to the clipboard.",
    L"Could not copy to the clipboard.",

    L"Could not save settings:\n",
    L"Restart ",
    L" now to apply the language to menus?",
    L"Could not restart automatically. Restart ",
    L" manually to apply the new language.",
};

static const Strings kEs{
    L"Ajustes de la aplicación",

    L"Idioma:",
    {L"Inglés", L"Español", L"Deutsch", L"Italiano", L"Français"},

    L"Tema:",
    L"Sistema (seguir Windows)",
    L"Claro",
    L"Oscuro",

    L"Tamaño de fuente:",
    {L"Predeterminado (tamaño del sistema)", L"+1 pt", L"+2 pt (recomendado)", L"+3 pt", L"+4 pt"},

    L"El tema y la fuente se aplican al instante. El idioma puede requerir reiniciar.",

    L"Claves de proveedor de IA…",
    L"Claves de proveedor de chat…",
    L"Exportar ajustes…",
    L"Importar ajustes…",

    L"Cifrar exportación (PME1 — este perfil de Windows)",

    L"Panel Explorador: interfaz completa de Windows. Puede verse clara en tema oscuro.",

    L"ID del equipo (para activar la licencia):",

    L"Copiar",

    L"Guardar",
    L"Cancelar",

    L"Error al exportar:\n",
    L"Ajustes exportados.",
    L"La importación sustituirá los ajustes actuales.\n\n¿Continuar?",
    L"Error al importar:\n",
    L"Ajustes importados.\n\n"
    L"Si algo no cambia, reinicie la app.",

    L"Huella copiada al portapapeles.",
    L"No se pudo copiar al portapapeles.",

    L"No se pudieron guardar los ajustes:\n",
    L"¿Reiniciar ",
    L" ahora para aplicar el idioma a los menús?",
    L"No se pudo reiniciar automáticamente. Reinicie ",
    L" manualmente para aplicar el nuevo idioma.",
};

static const Strings kDe{
    L"App-Einstellungen",

    L"Sprache:",
    {L"English", L"Español", L"Deutsch", L"Italiano", L"Français"},

    L"Thema:",
    L"System (Windows folgen)",
    L"Hell",
    L"Dunkel",

    L"Schriftgröße:",
    {L"Standard (Systemgröße)", L"+1 pt", L"+2 pt (empfohlen)", L"+3 pt", L"+4 pt"},

    L"Design und Schrift werden sofort angewendet. Sprache kann einen Neustart erfordern.",

    L"KI-Anbieter-Schlüssel…",
    L"Chat-Anbieter-Schlüssel…",
    L"Einstellungen exportieren…",
    L"Einstellungen importieren…",

    L"Export verschlüsseln",

    L"Explorer-Panel: Extended",

    L"Geräte-ID (für Lizenzaktivierung):",

    L"Kopieren",

    L"Speichern",
    L"Abbrechen",

    L"Export fehlgeschlagen:\n",
    L"Einstellungen exportiert.",
    L"Import ersetzt die aktuellen Einstellungen.\n\nFortfahren?",
    L"Import fehlgeschlagen:\n",
    L"Einstellungen importiert.\n\n"
    L"Wenn etwas unverändert wirkt, App neu starten.",

    L"Fingerprint in die Zwischenablage kopiert.",
    L"Kopieren in die Zwischenablage fehlgeschlagen.",

    L"Einstellungen konnten nicht gespeichert werden:\n",
    L"",
    L" jetzt neu starten, um die Sprache für Menüs anzuwenden?",
    L"Automatischer Neustart fehlgeschlagen. Bitte ",
    L" manuell neu starten.",
};

static const Strings kIt{
    L"Impostazioni app",

    L"Lingua:",
    {L"English", L"Español", L"Deutsch", L"Italiano", L"Français"},

    L"Tema:",
    L"Sistema (segui Windows)",
    L"Chiaro",
    L"Scuro",

    L"Dimensione carattere:",
    {L"Predefinita (dimensione di sistema)", L"+1 pt", L"+2 pt (consigliato)", L"+3 pt", L"+4 pt"},

    L"Tema e carattere si applicano subito. La lingua potrebbe richiedere un riavvio.",

    L"Chiavi provider IA…",
    L"Chiavi provider chat…",
    L"Esporta impostazioni…",
    L"Importa impostazioni…",

    L"Cifra esportazione (PME1 — questo profilo Windows)",

    L"Pannello Esplora: interfaccia completa di Windows. In tema scuro può restare chiara.",

    L"ID del PC (per attivare la licenza):",

    L"Copia",

    L"Salva",
    L"Annulla",

    L"Esportazione non riuscita:\n",
    L"Impostazioni esportate.",
    L"L'importazione sostituirà le impostazioni attuali.\n\nContinuare?",
    L"Importazione non riuscita:\n",
    L"Impostazioni importate.\n\n"
    L"Se qualcosa non cambia, riavvia l'app.",

    L"Impronta copiata negli appunti.",
    L"Impossibile copiare negli appunti.",

    L"Impossibile salvare le impostazioni:\n",
    L"Riavviare ",
    L" ora per applicare la lingua ai menu?",
    L"Riavvio automatico non riuscito. Riavvia ",
    L" manualmente.",
};

static const Strings kFr{
    L"Paramètres de l'application",

    L"Langue :",
    {L"English", L"Español", L"Deutsch", L"Italiano", L"Français"},

    L"Thème :",
    L"Système (suivre Windows)",
    L"Clair",
    L"Sombre",

    L"Taille de police :",
    {L"Par défaut (taille système)", L"+1 pt", L"+2 pt (recommandé)", L"+3 pt", L"+4 pt"},

    L"Thème et police : effet immédiat. La langue peut demander un redémarrage.",

    L"Clés du fournisseur IA…",
    L"Clés du fournisseur de chat…",
    L"Exporter les paramètres…",
    L"Importer les paramètres…",

    L"Chiffrer l'export",

    L"Panneau Explorateur : interface Windows complète. Peut rester claire en thème sombre.",

    L"ID machine (pour activer la licence) :",

    L"Copier",

    L"Enregistrer",
    L"Annuler",

    L"Échec de l'export :\n",
    L"Paramètres exportés.",
    L"L'import remplacera les paramètres actuels.\n\nContinuer ?",
    L"Échec de l'import :\n",
    L"Paramètres importés.\n\n"
    L"Si rien ne change, redémarrez l'app.",

    L"Empreinte copiée dans le presse-papiers.",
    L"Impossible de copier dans le presse-papiers.",

    L"Impossible d'enregistrer :\n",
    L"Redémarrer ",
    L" maintenant pour appliquer la langue aux menus ?",
    L"Redémarrage automatique impossible. Redémarrez ",
    L" manuellement.",
};

} // namespace

const Strings& strings_for(std::string_view display_language)
{
    const std::string c = norm(display_language);
    if (c == "es")
        return kEs;
    if (c == "de")
        return kDe;
    if (c == "it")
        return kIt;
    if (c == "fr")
        return kFr;
    return kEn;
}

} // namespace pmui::app_settings_i18n
