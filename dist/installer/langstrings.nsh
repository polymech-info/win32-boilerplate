; ============================================================================
; installer\langstrings.nsh — localised strings for the custom PATH page.
; Include this file AFTER all !insertmacro MUI_LANGUAGE lines.
; ============================================================================
;
; Four strings per language:
;   PATH_OPT_TITLE    — custom-page header title
;   PATH_OPT_SUBTITLE — custom-page header subtitle
;   PATH_OPT_CHECK    — checkbox label  ($(^Name) = product name at runtime)
;   PATH_OPT_DESC     — explanatory label ($INSTDIR expanded at runtime)
;
; EU official languages covered (all present in NSIS 3+):
;   English, German, French, Spanish, Italian, Portuguese, Dutch,
;   Polish, Czech, Slovak, Hungarian, Romanian, Bulgarian, Greek,
;   Swedish, Danish, Finnish, Estonian, Latvian, Lithuanian,
;   Slovenian, Croatian, Irish, Luxembourgish
; Bonus: Norwegian (EEA), Catalan, Basque, Galician
; ============================================================================

; --- English ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_ENGLISH} "Installation Options"
LangString PATH_OPT_SUBTITLE ${LANG_ENGLISH} "Choose additional tasks to perform."
LangString PATH_OPT_CHECK    ${LANG_ENGLISH} "Add $(^Name) to the PATH environment variable"
LangString PATH_OPT_DESC     ${LANG_ENGLISH} "Adds $\"$INSTDIR$\" to your PATH so you can run pm-image from any Command Prompt or PowerShell window without typing the full path."

; --- German -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_GERMAN} "Installationsoptionen"
LangString PATH_OPT_SUBTITLE ${LANG_GERMAN} "Wählen Sie zusätzliche Aufgaben aus."
LangString PATH_OPT_CHECK    ${LANG_GERMAN} "$(^Name) zur PATH-Umgebungsvariable hinzufügen"
LangString PATH_OPT_DESC     ${LANG_GERMAN} "Fügt $\"$INSTDIR$\" zu PATH hinzu, damit pm-image aus jedem Eingabeaufforderungs- oder PowerShell-Fenster gestartet werden kann."

; --- French -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_FRENCH} "Options d'installation"
LangString PATH_OPT_SUBTITLE ${LANG_FRENCH} "Choisissez des tâches supplémentaires."
LangString PATH_OPT_CHECK    ${LANG_FRENCH} "Ajouter $(^Name) à la variable d'environnement PATH"
LangString PATH_OPT_DESC     ${LANG_FRENCH} "Ajoute $\"$INSTDIR$\" à PATH pour permettre l'exécution de pm-image depuis n'importe quelle invite de commandes ou fenêtre PowerShell."

; --- Spanish ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_SPANISH} "Opciones de instalación"
LangString PATH_OPT_SUBTITLE ${LANG_SPANISH} "Elija tareas adicionales a realizar."
LangString PATH_OPT_CHECK    ${LANG_SPANISH} "Agregar $(^Name) a la variable de entorno PATH"
LangString PATH_OPT_DESC     ${LANG_SPANISH} "Añade $\"$INSTDIR$\" al PATH para que pm-image pueda ejecutarse desde cualquier símbolo del sistema o ventana de PowerShell."

LangString PATH_OPT_TITLE    ${LANG_SPANISHINTERNATIONAL} "Opciones de instalación"
LangString PATH_OPT_SUBTITLE ${LANG_SPANISHINTERNATIONAL} "Elija tareas adicionales a realizar."
LangString PATH_OPT_CHECK    ${LANG_SPANISHINTERNATIONAL} "Agregar $(^Name) a la variable de entorno PATH"
LangString PATH_OPT_DESC     ${LANG_SPANISHINTERNATIONAL} "Añade $\"$INSTDIR$\" al PATH para que pm-image pueda ejecutarse desde cualquier símbolo del sistema o ventana de PowerShell."

; --- Italian ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_ITALIAN} "Opzioni di installazione"
LangString PATH_OPT_SUBTITLE ${LANG_ITALIAN} "Scegliere le attività aggiuntive da eseguire."
LangString PATH_OPT_CHECK    ${LANG_ITALIAN} "Aggiungere $(^Name) alla variabile d'ambiente PATH"
LangString PATH_OPT_DESC     ${LANG_ITALIAN} "Aggiunge $\"$INSTDIR$\" al PATH in modo da poter eseguire pm-image da qualsiasi prompt dei comandi o finestra PowerShell."

; --- Portuguese (Portugal) --------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_PORTUGUESE} "Opções de instalação"
LangString PATH_OPT_SUBTITLE ${LANG_PORTUGUESE} "Escolha tarefas adicionais a executar."
LangString PATH_OPT_CHECK    ${LANG_PORTUGUESE} "Adicionar $(^Name) à variável de ambiente PATH"
LangString PATH_OPT_DESC     ${LANG_PORTUGUESE} "Adiciona $\"$INSTDIR$\" ao PATH para que pm-image possa ser executado a partir de qualquer janela do Prompt de Comando ou PowerShell."

LangString PATH_OPT_TITLE    ${LANG_PORTUGUESEBR} "Opções de instalação"
LangString PATH_OPT_SUBTITLE ${LANG_PORTUGUESEBR} "Escolha tarefas adicionais a executar."
LangString PATH_OPT_CHECK    ${LANG_PORTUGUESEBR} "Adicionar $(^Name) à variável de ambiente PATH"
LangString PATH_OPT_DESC     ${LANG_PORTUGUESEBR} "Adiciona $\"$INSTDIR$\" ao PATH para que pm-image possa ser executado a partir de qualquer janela do Prompt de Comando ou PowerShell."

; --- Dutch ------------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_DUTCH} "Installatieopties"
LangString PATH_OPT_SUBTITLE ${LANG_DUTCH} "Kies extra taken om uit te voeren."
LangString PATH_OPT_CHECK    ${LANG_DUTCH} "$(^Name) toevoegen aan de PATH-omgevingsvariabele"
LangString PATH_OPT_DESC     ${LANG_DUTCH} "Voegt $\"$INSTDIR$\" toe aan PATH zodat pm-image vanuit elk opdrachtprompt- of PowerShell-venster kan worden gestart."

; --- Polish -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_POLISH} "Opcje instalacji"
LangString PATH_OPT_SUBTITLE ${LANG_POLISH} "Wybierz dodatkowe zadania do wykonania."
LangString PATH_OPT_CHECK    ${LANG_POLISH} "Dodaj $(^Name) do zmiennej środowiskowej PATH"
LangString PATH_OPT_DESC     ${LANG_POLISH} "Dodaje $\"$INSTDIR$\" do zmiennej PATH, umożliwiając uruchamianie pm-image z dowolnego wiersza poleceń lub okna PowerShell."

; --- Czech ------------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_CZECH} "Možnosti instalace"
LangString PATH_OPT_SUBTITLE ${LANG_CZECH} "Vyberte další úkoly, které mají být provedeny."
LangString PATH_OPT_CHECK    ${LANG_CZECH} "Přidat $(^Name) do proměnné prostředí PATH"
LangString PATH_OPT_DESC     ${LANG_CZECH} "Přidá $\"$INSTDIR$\" do proměnné PATH, aby bylo možné spustit pm-image z libovolného příkazového řádku nebo okna PowerShell."

; --- Slovak -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_SLOVAK} "Možnosti inštalácie"
LangString PATH_OPT_SUBTITLE ${LANG_SLOVAK} "Vyberte ďalšie úlohy, ktoré sa majú vykonať."
LangString PATH_OPT_CHECK    ${LANG_SLOVAK} "Pridať $(^Name) do premennej prostredia PATH"
LangString PATH_OPT_DESC     ${LANG_SLOVAK} "Pridá $\"$INSTDIR$\" do premennej PATH, aby bolo možné spustiť pm-image z ľubovoľného príkazového riadka alebo okna PowerShell."

; --- Hungarian --------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_HUNGARIAN} "Telepítési beállítások"
LangString PATH_OPT_SUBTITLE ${LANG_HUNGARIAN} "Válasszon további elvégzendő feladatokat."
LangString PATH_OPT_CHECK    ${LANG_HUNGARIAN} "$(^Name) hozzáadása a PATH környezeti változóhoz"
LangString PATH_OPT_DESC     ${LANG_HUNGARIAN} "Hozzáadja $\"$INSTDIR$\" a PATH-hoz, így a pm-image bármely parancssorból vagy PowerShell ablakból futtatható."

; --- Romanian ---------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_ROMANIAN} "Opțiuni de instalare"
LangString PATH_OPT_SUBTITLE ${LANG_ROMANIAN} "Alegeți sarcini suplimentare de efectuat."
LangString PATH_OPT_CHECK    ${LANG_ROMANIAN} "Adăugați $(^Name) la variabila de mediu PATH"
LangString PATH_OPT_DESC     ${LANG_ROMANIAN} "Adaugă $\"$INSTDIR$\" la PATH pentru a rula pm-image din orice fereastră Command Prompt sau PowerShell."

; --- Bulgarian --------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_BULGARIAN} "Опции за инсталиране"
LangString PATH_OPT_SUBTITLE ${LANG_BULGARIAN} "Изберете допълнителни задачи за изпълнение."
LangString PATH_OPT_CHECK    ${LANG_BULGARIAN} "Добавяне на $(^Name) към PATH"
LangString PATH_OPT_DESC     ${LANG_BULGARIAN} "Добавя $\"$INSTDIR$\" към PATH, за да може pm-image да се стартира от всеки команден ред или прозорец на PowerShell."

; --- Greek ------------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_GREEK} "Επιλογές εγκατάστασης"
LangString PATH_OPT_SUBTITLE ${LANG_GREEK} "Επιλέξτε πρόσθετες εργασίες προς εκτέλεση."
LangString PATH_OPT_CHECK    ${LANG_GREEK} "Προσθήκη $(^Name) στη μεταβλητή περιβάλλοντος PATH"
LangString PATH_OPT_DESC     ${LANG_GREEK} "Προσθέτει $\"$INSTDIR$\" στο PATH ώστε το pm-image να εκτελείται από οποιοδήποτε παράθυρο γραμμής εντολών ή PowerShell."

; --- Swedish ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_SWEDISH} "Installationsalternativ"
LangString PATH_OPT_SUBTITLE ${LANG_SWEDISH} "Välj ytterligare uppgifter att utföra."
LangString PATH_OPT_CHECK    ${LANG_SWEDISH} "Lägg till $(^Name) i PATH-miljövariabeln"
LangString PATH_OPT_DESC     ${LANG_SWEDISH} "Lägger till $\"$INSTDIR$\" i PATH så att pm-image kan köras från valfritt kommandofönster eller PowerShell-fönster."

; --- Danish -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_DANISH} "Installationsindstillinger"
LangString PATH_OPT_SUBTITLE ${LANG_DANISH} "Vælg yderligere opgaver at udføre."
LangString PATH_OPT_CHECK    ${LANG_DANISH} "Tilføj $(^Name) til PATH-miljøvariablen"
LangString PATH_OPT_DESC     ${LANG_DANISH} "Tilføjer $\"$INSTDIR$\" til PATH, så pm-image kan køres fra ethvert kommandoprompt- eller PowerShell-vindue."

; --- Finnish ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_FINNISH} "Asennusvaihtoehdot"
LangString PATH_OPT_SUBTITLE ${LANG_FINNISH} "Valitse suoritettavat lisätehtävät."
LangString PATH_OPT_CHECK    ${LANG_FINNISH} "Lisää $(^Name) PATH-ympäristömuuttujaan"
LangString PATH_OPT_DESC     ${LANG_FINNISH} "Lisää $\"$INSTDIR$\" PATH-muuttujaan, jolloin pm-image voidaan suorittaa mistä tahansa komentokehote- tai PowerShell-ikkunasta."

; --- Estonian ---------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_ESTONIAN} "Installimisvalikud"
LangString PATH_OPT_SUBTITLE ${LANG_ESTONIAN} "Valige täiendavad toimingud."
LangString PATH_OPT_CHECK    ${LANG_ESTONIAN} "Lisa $(^Name) PATH keskkonna muutujasse"
LangString PATH_OPT_DESC     ${LANG_ESTONIAN} "Lisab $\"$INSTDIR$\" PATH-i, et pm-image saaks käivitada mistahes käsuviiba- või PowerShell-aknas."

; --- Latvian ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_LATVIAN} "Instalācijas opcijas"
LangString PATH_OPT_SUBTITLE ${LANG_LATVIAN} "Izvēlieties papildu uzdevumus."
LangString PATH_OPT_CHECK    ${LANG_LATVIAN} "Pievienot $(^Name) PATH mainīgajam"
LangString PATH_OPT_DESC     ${LANG_LATVIAN} "Pievieno $\"$INSTDIR$\" PATH, lai pm-image varētu palaist no jebkura komanduzvednes vai PowerShell loga."

; --- Lithuanian -------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_LITHUANIAN} "Diegimo parinktys"
LangString PATH_OPT_SUBTITLE ${LANG_LITHUANIAN} "Pasirinkite papildomus veiksmus."
LangString PATH_OPT_CHECK    ${LANG_LITHUANIAN} "Pridėti $(^Name) prie PATH aplinkos kintamojo"
LangString PATH_OPT_DESC     ${LANG_LITHUANIAN} "Prideda $\"$INSTDIR$\" prie PATH, kad pm-image būtų galima paleisti iš bet kurio komandų eilutės ar PowerShell lango."

; --- Slovenian --------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_SLOVENIAN} "Možnosti namestitve"
LangString PATH_OPT_SUBTITLE ${LANG_SLOVENIAN} "Izberite dodatna opravila."
LangString PATH_OPT_CHECK    ${LANG_SLOVENIAN} "Dodaj $(^Name) v spremenljivko okolja PATH"
LangString PATH_OPT_DESC     ${LANG_SLOVENIAN} "Doda $\"$INSTDIR$\" v PATH, da je pm-image mogoče zagnati iz katerega koli ukaznega poziva ali okna PowerShell."

; --- Croatian ---------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_CROATIAN} "Opcije instalacije"
LangString PATH_OPT_SUBTITLE ${LANG_CROATIAN} "Odaberite dodatne zadatke."
LangString PATH_OPT_CHECK    ${LANG_CROATIAN} "Dodaj $(^Name) varijabli okoline PATH"
LangString PATH_OPT_DESC     ${LANG_CROATIAN} "Dodaje $\"$INSTDIR$\" u PATH kako bi pm-image mogao biti pokrenut iz bilo kojeg naredbenog retka ili prozora PowerShell."

; --- Irish ------------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_IRISH} "Roghanna Suiteála"
LangString PATH_OPT_SUBTITLE ${LANG_IRISH} "Roghnaigh tascanna breise le déanamh."
LangString PATH_OPT_CHECK    ${LANG_IRISH} "Cuir $(^Name) le hathróg timpeallachta PATH"
LangString PATH_OPT_DESC     ${LANG_IRISH} "Cuireann sé $\"$INSTDIR$\" le PATH ionas gur féidir pm-image a rith ó aon fhuinneog Leid Ordú nó PowerShell."

; --- Luxembourgish ----------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_LUXEMBOURGISH} "Installatiounsoptiounen"
LangString PATH_OPT_SUBTITLE ${LANG_LUXEMBOURGISH} "Wielt zousätzlech Aufgaben."
LangString PATH_OPT_CHECK    ${LANG_LUXEMBOURGISH} "$(^Name) zur PATH-Ëmweltvariable bäisetzen"
LangString PATH_OPT_DESC     ${LANG_LUXEMBOURGISH} "Setzt $\"$INSTDIR$\" op PATH, fir pm-image aus all Command Prompt oder PowerShell-Fënster auszeféieren."

; --- Norwegian (Bokmål) — EEA -----------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_NORWEGIAN} "Installasjonsalternativer"
LangString PATH_OPT_SUBTITLE ${LANG_NORWEGIAN} "Velg ytterligere oppgaver å utføre."
LangString PATH_OPT_CHECK    ${LANG_NORWEGIAN} "Legg til $(^Name) i PATH-miljøvariabelen"
LangString PATH_OPT_DESC     ${LANG_NORWEGIAN} "Legger til $\"$INSTDIR$\" i PATH slik at pm-image kan kjøres fra ethvert kommandovindu eller PowerShell-vindu."

LangString PATH_OPT_TITLE    ${LANG_NORWEGIANNYNORSK} "Installasjonsalternativ"
LangString PATH_OPT_SUBTITLE ${LANG_NORWEGIANNYNORSK} "Vel fleire oppgåver å utføra."
LangString PATH_OPT_CHECK    ${LANG_NORWEGIANNYNORSK} "Legg til $(^Name) i PATH-miljøvariabelen"
LangString PATH_OPT_DESC     ${LANG_NORWEGIANNYNORSK} "Legg til $\"$INSTDIR$\" i PATH slik at pm-image kan køyrast frå eit kvart kommandovindauge eller PowerShell-vindauge."

; --- Catalan ----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_CATALAN} "Opcions d'instal·lació"
LangString PATH_OPT_SUBTITLE ${LANG_CATALAN} "Trieu tasques addicionals a realitzar."
LangString PATH_OPT_CHECK    ${LANG_CATALAN} "Afegiu $(^Name) a la variable d'entorn PATH"
LangString PATH_OPT_DESC     ${LANG_CATALAN} "Afegeix $\"$INSTDIR$\" al PATH perquè pm-image es pugui executar des de qualsevol finestra de l'indicador d'ordres o PowerShell."

; --- Galician ---------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_GALICIAN} "Opcións de instalación"
LangString PATH_OPT_SUBTITLE ${LANG_GALICIAN} "Escolla tarefas adicionais a realizar."
LangString PATH_OPT_CHECK    ${LANG_GALICIAN} "Engadir $(^Name) á variable de ambiente PATH"
LangString PATH_OPT_DESC     ${LANG_GALICIAN} "Engade $\"$INSTDIR$\" ao PATH para que pm-image poida executarse dende calquera ventá de símbolo do sistema ou PowerShell."

; --- Basque -----------------------------------------------------------------
LangString PATH_OPT_TITLE    ${LANG_BASQUE} "Instalazio aukerak"
LangString PATH_OPT_SUBTITLE ${LANG_BASQUE} "Hautatu egiteko lan osagarriak."
LangString PATH_OPT_CHECK    ${LANG_BASQUE} "Gehitu $(^Name) PATH ingurune-aldagaira"
LangString PATH_OPT_DESC     ${LANG_BASQUE} "$\"$INSTDIR$\" PATH-era gehitzen du, pm-image edozein Komando Gonbidapen edo PowerShell leihotik exekutatu ahal izateko."
