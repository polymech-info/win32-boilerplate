#include "helpers/provider_dlg_i18n.hpp"

#include <string>

namespace pmui::provider_dlg_i18n {
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
    L"AI Provider Settings",
    L"API key:",
    L"Show",
    L"Hide",
    L"Base URL:",
    L"\u26BF Keys are stored in %APPDATA%\\",
    L"\\settings.json",
    L"Save",
    L"Cancel",
    L"Failed to save provider settings:\n",
    L"Could not load provider settings:\n",
};

static const Strings kEs{
    L"Ajustes del proveedor de IA",
    L"Clave API:",
    L"Mostrar",
    L"Ocultar",
    L"URL base:",
    L"\u26BF Las claves se cifran con libsodium + DPAPI y se guardan en %APPDATA%\\",
    L"\\settings.json",
    L"Guardar",
    L"Cancelar",
    L"No se pudieron guardar los ajustes del proveedor:\n",
    L"No se pudieron cargar los ajustes del proveedor:\n",
};

static const Strings kDe{
    L"KI-Anbieter-Einstellungen",
    L"API-Schlüssel:",
    L"Anzeigen",
    L"Ausblenden",
    L"Basis-URL:",
    L"\u26BF Schlüssel werden mit libsodium + DPAPI verschlüsselt und gespeichert unter %APPDATA%\\",
    L"\\settings.json",
    L"Speichern",
    L"Abbrechen",
    L"Speichern der Anbieter-Einstellungen fehlgeschlagen:\n",
    L"Laden der Anbieter-Einstellungen fehlgeschlagen:\n",
};

static const Strings kIt{
    L"Impostazioni provider IA",
    L"Chiave API:",
    L"Mostra",
    L"Nascondi",
    L"URL base:",
    L"\u26BF Le chiavi sono cifrate con libsodium + DPAPI e salvate in %APPDATA%\\",
    L"\\settings.json",
    L"Salva",
    L"Annulla",
    L"Salvataggio impostazioni provider non riuscito:\n",
    L"Impossibile caricare le impostazioni del provider:\n",
};

static const Strings kFr{
    L"Paramètres du fournisseur IA",
    L"Clé API :",
    L"Afficher",
    L"Masquer",
    L"URL de base :",
    L"\u26BF Les clés sont chiffrées avec libsodium + DPAPI et enregistrées dans %APPDATA%\\",
    L"\\settings.json",
    L"Enregistrer",
    L"Annuler",
    L"Échec de l'enregistrement des paramètres du fournisseur :\n",
    L"Impossible de charger les paramètres du fournisseur :\n",
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

} // namespace pmui::provider_dlg_i18n
