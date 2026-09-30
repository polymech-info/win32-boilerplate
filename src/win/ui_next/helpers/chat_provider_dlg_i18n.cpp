#include "helpers/chat_provider_dlg_i18n.hpp"

#include <string>

namespace pmui::chat_provider_dlg_i18n {
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
    L"Chat Provider Settings",
    L"Provider:",
    L"Model:",
    L"API key:",
    L"Base URL:",
    L"Max iterations:",
    L"Image provider:",
    L"Image model:",
    L"Video provider:",
    L"Video model:",
    L"Provider:",
    L"Recognition model:",
    L"Collection:",
    L"Image Creation Provider & Model",
    L"Video Generation (path tool)",
    L"Image Recognition Models",
    L"Chat (text & LLM)",
    L"Agent Settings",
    L"Voice & Audio",
    L"Voice input provider:",
    L"Voice input model:",
    L"Voice output provider:",
    L"Voice output model:",
    L"Voice output voice:",
    L"Refresh",
    L"Default base URL: ",
    L"Custom router — enter your endpoint base URL below.",
    L"\u26BF Saved (encrypted) to %APPDATA%\\",
    L"\\settings.json[\"chat\"].\n"
    L"  Most providers (OpenRouter / Anthropic / Ollama / vLLM) speak the OpenAI Chat Completions protocol.",
    L"Show",
    L"Hide",
    L"Save",
    L"Cancel",
    L"Failed to save chat provider settings:\n",
};

static const Strings kEs{
    L"Ajustes del proveedor de chat",
    L"Enrutador:",
    L"Modelo:",
    L"Clave API:",
    L"URL base:",
    L"Máx. iter:",
    L"Proveedor de imagen:",
    L"Modelo de imagen:",
    L"Proveedor de video:",
    L"Modelo de video:",
    L"Proveedor:",
    L"Modelo de reconocimiento:",
    L"Colección:",
    L"Proveedor y modelo de creación de imágenes",
    L"Generación de video (herramienta de ruta)",
    L"Modelos de reconocimiento de imágenes",
    L"Chat (texto y LLM)",
    L"Ajustes del agente",
    L"Voz y audio",
    L"Proveedor entrada de voz:",
    L"Modelo entrada de voz:",
    L"Proveedor salida de voz:",
    L"Modelo salida de voz:",
    L"Voz salida:",
    L"Actualizar",
    L"URL base predeterminada: ",
    L"Enrutador personalizado: indique la URL base abajo.",
    L"\u26BF Guardado (cifrado) en %APPDATA%\\",
    L"\\settings.json[\"chat\"].\n"
    L"  La mayoría de proveedores usan el protocolo OpenAI Chat Completions.",
    L"Mostrar",
    L"Ocultar",
    L"Guardar",
    L"Cancelar",
    L"No se pudieron guardar los ajustes del chat:\n",
};

static const Strings kDe{
    L"Chat-Anbieter-Einstellungen",
    L"Router:",
    L"Modell:",
    L"API-Schlüssel:",
    L"Basis-URL:",
    L"Max. Iter.:",
    L"Bildanbieter:",
    L"Bildmodell:",
    L"Videoanbieter:",
    L"Videomodell:",
    L"Anbieter:",
    L"Erkennungsmodell:",
    L"Sammlung:",
    L"Bildgenerierung: Anbieter & Modell",
    L"Videogenerierung (Pfadwerkzeug)",
    L"Bilderkennungsmodelle",
    L"Chat (Text & LLM)",
    L"Agent-Einstellungen",
    L"Sprache & Audio",
    L"Spracheingabe-Anbieter:",
    L"Spracheingabe-Modell:",
    L"Sprachausgabe-Anbieter:",
    L"Sprachausgabe-Modell:",
    L"Sprachausgabe-Stimme:",
    L"Aktualisieren",
    L"Standard-Basis-URL: ",
    L"Benutzerdefiniert — Basis-URL unten eingeben.",
    L"\u26BF Gespeichert (verschlüsselt) in %APPDATA%\\",
    L"\\settings.json[\"chat\"].\n"
    L"  Die meisten Anbieter nutzen das OpenAI-Chat-Completions-Protokoll.",
    L"Anzeigen",
    L"Ausblenden",
    L"Speichern",
    L"Abbrechen",
    L"Speichern der Chat-Einstellungen fehlgeschlagen:\n",
};

static const Strings kIt{
    L"Impostazioni provider chat",
    L"Router:",
    L"Modello:",
    L"Chiave API:",
    L"URL base:",
    L"Max iter:",
    L"Provider immagini:",
    L"Modello immagini:",
    L"Provider video:",
    L"Modello video:",
    L"Provider:",
    L"Modello riconoscimento:",
    L"Raccolta:",
    L"Provider e modello creazione immagini",
    L"Generazione video (strumento percorso)",
    L"Modelli di riconoscimento immagini",
    L"Chat (testo e LLM)",
    L"Impostazioni agente",
    L"Voce e audio",
    L"Provider input vocale:",
    L"Modello input vocale:",
    L"Provider output vocale:",
    L"Modello output vocale:",
    L"Voce output:",
    L"Aggiorna",
    L"URL base predefinita: ",
    L"Router personalizzato — inserire l'URL base sotto.",
    L"\u26BF Salvato (cifrato) in %APPDATA%\\",
    L"\\settings.json[\"chat\"].\n"
    L"  La maggior parte dei provider usa il protocollo OpenAI Chat Completions.",
    L"Mostra",
    L"Nascondi",
    L"Salva",
    L"Annulla",
    L"Salvataggio impostazioni chat non riuscito:\n",
};

static const Strings kFr{
    L"Paramètres du fournisseur chat",
    L"Routeur :",
    L"Modèle :",
    L"Clé API :",
    L"URL de base :",
    L"Itér. max :",
    L"Fournisseur image :",
    L"Modèle image :",
    L"Fournisseur vidéo :",
    L"Modèle vidéo :",
    L"Fournisseur :",
    L"Modèle de reconnaissance :",
    L"Collection :",
    L"Fournisseur et modèle de création d'images",
    L"Génération vidéo (outil chemin)",
    L"Modèles de reconnaissance d'images",
    L"Chat (texte et LLM)",
    L"Paramètres de l'agent",
    L"Voix et audio",
    L"Fournisseur entrée vocale :",
    L"Modèle entrée vocale :",
    L"Fournisseur sortie vocale :",
    L"Modèle sortie vocale :",
    L"Voix sortie :",
    L"Actualiser",
    L"URL de base par défaut : ",
    L"Routeur personnalisé — saisissez l'URL de base ci-dessous.",
    L"\u26BF Enregistré (chiffré) dans %APPDATA%\\",
    L"\\settings.json[\"chat\"].\n"
    L"  La plupart des fournisseurs utilisent le protocole OpenAI Chat Completions.",
    L"Afficher",
    L"Masquer",
    L"Enregistrer",
    L"Annuler",
    L"Échec de l'enregistrement du fournisseur chat :\n",
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

} // namespace pmui::chat_provider_dlg_i18n
