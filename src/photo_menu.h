#pragma once
// Photo Mode menu model (Phase 2): pure data and navigation, no game or D3D dependencies, unit tested in test_core.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

enum MenuTab { TAB_COMPOSE, TAB_SETTINGS, TAB_FX, TAB_LIGHTS, TAB_PHOTOS, TAB_COUNT };
enum ItemKind { KIND_CHOICE, KIND_RANGE, KIND_ACTION };
enum ItemId {
    // COMPONER
    IT_GRID, IT_FRAME, IT_HIDE_PLAYER, IT_HIDE_PEOPLE, IT_POSE, IT_POSE_FRAME, IT_EXPRESSION, IT_LOOK_AT, IT_WEAPON, IT_TIME, IT_WEATHER, IT_SLOWMO, IT_NEXT_FRAME, IT_LOGO, IT_LOGO_POS, IT_LOGO_SIZE, IT_PRESET_SLOT, IT_PRESET_SAVE, IT_PRESET_LOAD, IT_CAMERA_MODE,
    // AJUSTES
    IT_MOVE_SPEED, IT_LOOK_SPEED, IT_INVERT_Y, IT_MAX_DISTANCE, IT_COLLISION, IT_OPEN_BUTTON, IT_MENU_BUTTON, IT_PROMPTS, IT_STATUS, IT_ACCENT, IT_MENU_BG, IT_ICONS,
    IT_SOUNDS, IT_MUSIC, IT_MUSIC_VOLUME, IT_MOUSE_LOOK, IT_LANGUAGE, IT_CUTSCENE_CAMERA, IT_PHOTO_FOLDER, IT_GALLERY_COPY, IT_SETTINGS_RESET,
    // EFECTOS
    IT_FILTER, IT_EXPOSURE, IT_CONTRAST, IT_SATURATION, IT_TEMPERATURE, IT_VIGNETTE, IT_GRAIN, IT_TILT, IT_TILT_FOCUS, IT_LENS_DROPS, IT_SKYGFX, IT_CHROMA, IT_BLOOM, IT_DISTORTION, IT_RADIAL_BLUR, IT_HAZE, IT_FISHEYE, IT_FISHEYE_CIRCLE, IT_FX_RESET,
    // LUCES
    IT_LIGHT_SLOT, IT_LIGHT_TYPE, IT_LIGHT_PLACE, IT_LIGHT_MOVE, IT_LIGHT_MOVE_MODE, IT_LIGHT_ON, IT_LIGHT_COLOR, IT_LIGHT_INTENSITY, IT_LIGHT_RANGE, IT_LIGHT_HUE, IT_LIGHT_SAT, IT_LIGHT_SHADOWS, IT_LIGHT_REMOVE, IT_LIGHT_CLEAR,
    IT_COUNT
};
struct MenuItem {
    ItemId id; MenuTab tab; ItemKind kind; const char* label; const char* key;
    float min, max, step, def; std::vector<std::string> choices; bool preset;   // preset: saved in Custom 1-3
};

inline const char* const tabNames[TAB_COUNT] = {"COMPONER", "AJUSTES", "EFECTOS", "LUCES", "FOTOS"};
inline const char* const weatherNames[] = {"Sin cambio", "Muy soleado LS", "Soleado LS", "Smog LS", "Soleado con smog LS", "Nublado LS",
    "Soleado SF", "Muy soleado SF", "Nublado SF", "Lluvia SF", "Niebla SF", "Soleado LV", "Muy soleado LV", "Nublado LV",
    "Muy soleado campo", "Soleado campo", "Nublado campo", "Lluvia campo", "Muy soleado desierto", "Soleado desierto",
    "Tormenta de arena", "Bajo el agua", "Color extra 1", "Color extra 2"};
// CJ pose library: an animation from the game's IFP blocks, frozen at a chosen point (photo_phase2.inc applies it).
// COMPONER > Arma en la mano: only the weapon's model goes into CJ's hand for the photo (his inventory is not touched).
// type is the game's eWeaponType; -1 keeps what he holds, 0 empties the hand.
struct WeaponChoice { int type; const char* label; };
inline const WeaponChoice weaponChoices[] = {{-1, "Sin cambio"}, {0, "Ninguna"}, {5, "Bate"}, {4, "Cuchillo"}, {8, "Katana"}, {9, "Motosierra"},
    {15, "Bastón"}, {14, "Flores"}, {22, "Pistola"}, {23, "Pistola con silenciador"}, {24, "Desert Eagle"}, {25, "Escopeta"},
    {26, "Recortada"}, {27, "Escopeta de combate"}, {28, "Uzi"}, {32, "Tec-9"}, {29, "MP5"}, {30, "AK-47"}, {31, "M4"},
    {33, "Rifle"}, {34, "Francotirador"}, {35, "RPG"}, {37, "Lanzallamas"}, {38, "Minigun"}, {41, "Aerosol"}, {43, "Cámara de fotos"}};
constexpr int WEAPON_CHOICE_COUNT = static_cast<int>(sizeof(weaponChoices) / sizeof(weaponChoices[0]));
inline std::vector<std::string> weaponNames() { std::vector<std::string> n; for (const WeaponChoice& w : weaponChoices) n.push_back(w.label); return n; }
struct PoseDef { const char* block; const char* anim; const char* label; bool ground = false; };   // ground: rests on the floor (custom poses)
inline const PoseDef poseDefs[] = {
    {"", "", "Normal"},
    {"ped", "WALK_gang1", "Caminando (pandilla)"}, {"ped", "WALK_player", "Caminando"}, {"ped", "run_gang1", "Corriendo (pandilla)"},
    {"ped", "sprint_civi", "Sprint"}, {"ped", "JUMP_glide", "Saltando"}, {"ped", "FALL_fall", "Cayendo"}, {"ped", "IDLE_tired", "Cansado"},
    {"ped", "handsup", "Manos arriba"}, {"ped", "phone_talk", "Hablando por teléfono"}, {"ped", "XPRESSscratch", "Rascándose"},
    {"ghands", "gsign1", "Seña de pandilla 1"}, {"ghands", "gsign2", "Seña de pandilla 2"}, {"ghands", "gsign5", "Seña de pandilla 3"},
    {"gangs", "prtial_gngtlkA", "Platicando"}, {"gangs", "leanIDLE", "Recargado"}, {"gangs", "smkcig_prtl", "Fumando"},
    {"smoking", "M_smklean_loop", "Fumando recargado"}, {"beach", "ParkSit_M_loop", "Sentado"}, {"beach", "Lay_Bac_Loop", "Acostado"},
    {"dancing", "dnce_M_a", "Bailando 1"}, {"dancing", "dnce_M_b", "Bailando 2"}, {"rapping", "RAP_A_Loop", "Rapeando"},
    {"shop", "SHP_Gun_Aim", "Apuntando"}, {"camera", "camstnd_idleloop", "Tomando foto"}, {"on_lookers", "point_loop", "Señalando"},
    {"on_lookers", "wave_loop", "Saludando"}, {"on_lookers", "shout_01", "Gritando"}, {"park", "Tai_Chi_Loop", "Tai chi"},
    {"riot", "RIOT_ANGRY", "Enojado"}, {"playidles", "stretch", "Estirándose"}, {"crack", "crckdeth2", "Tirado"},
    // Aiming and shooting stances from the weapon animation files (upper body; pair them with COMPONER > Arma en la mano).
    {"ped", "GUN_STAND", "Apuntando con pistola"}, {"colt45", "colt45_fire_2hands", "Pistola a dos manos"},
    {"colt45", "colt45_crouchfire", "Pistola agachado"}, {"python", "python_fire", "Desert Eagle disparando"},
    {"python", "python_crouchfire", "Desert Eagle agachado"}, {"uzi", "UZI_fire", "Subfusil disparando"},
    {"rifle", "RIFLE_fire", "Rifle disparando"}, {"rifle", "RIFLE_crouchfire", "Rifle agachado"},
};
// CJ expressions: facial animations from ped.ifp, applied as a partial animation over the pose.
struct ExpressionDef { const char* anim; const char* label; };
inline const ExpressionDef expressionDefs[] = {{"", "Normal"}, {"facanger", "Enojado"}, {"facsurp", "Sorprendido"}, {"facsurpm", "Asustado"},
    {"factalk", "Hablando"}, {"facgum", "Chicle"}, {"facurios", "Curioso"}};
constexpr int EXPRESSION_COUNT = static_cast<int>(sizeof(expressionDefs) / sizeof(expressionDefs[0]));
inline std::vector<std::string> expressionNames() { std::vector<std::string> n; for (const ExpressionDef& e : expressionDefs) n.push_back(e.label); return n; }
constexpr int POSE_COUNT = static_cast<int>(sizeof(poseDefs) / sizeof(poseDefs[0]));
// Photo lights (Alan Wake 2 style): colour presets, linear RGB.
struct LightColor { const char* label; float r, g, b; };
inline const LightColor lightColors[] = {{"Blanco cálido", 1.f, .82f, .6f}, {"Blanco frío", .75f, .85f, 1.f}, {"Rojo", 1.f, .12f, .08f}, {"Azul", .2f, .35f, 1.f},
    {"Verde", .2f, 1.f, .3f}, {"Naranja", 1.f, .48f, .1f}, {"Morado", .6f, .2f, 1.f}, {"Rosa", 1.f, .3f, .7f}, {"Amarillo", 1.f, .9f, .2f},
    {"Personalizado", 1.f, 1.f, 1.f}};   // the last one takes LUCES > Tono / Saturación of the color
constexpr int LIGHT_COLOR_COUNT = static_cast<int>(sizeof(lightColors) / sizeof(lightColors[0]));
constexpr int PHOTO_LIGHTS = 3;
constexpr int LIGHT_COLOR_CUSTOM = LIGHT_COLOR_COUNT - 1;
// Hue in degrees (0 red, 120 green, 240 blue), saturation 0..1, full brightness: the light's intensity sets how bright it is.
struct Rgb { float r, g, b; };
inline Rgb hueColor(float hue, float sat) {
    float h = std::fmod(std::fmod(hue, 360.f) + 360.f, 360.f) / 60.f, x = 1.f - std::fabs(std::fmod(h, 2.f) - 1.f);
    Rgb c = h < 1 ? Rgb{1, x, 0} : h < 2 ? Rgb{x, 1, 0} : h < 3 ? Rgb{0, 1, x} : h < 4 ? Rgb{0, x, 1} : h < 5 ? Rgb{x, 0, 1} : Rgb{1, 0, x};
    sat = std::clamp(sat, 0.f, 1.f);
    return {1.f - sat + sat * c.r, 1.f - sat + sat * c.g, 1.f - sat + sat * c.b};
}
inline std::vector<std::string> lightColorNames() { std::vector<std::string> n; for (const LightColor& c : lightColors) n.push_back(c.label); return n; }
inline const float slowmoScales[] = {0.f, 0.05f, 0.1f, 0.25f, 0.5f};   // index 0 = frozen
inline const float frameAspects[] = {0.f, 2.39f, 1.85f, 16.f/9.f, 4.f/3.f, 1.f, 4.f/5.f};
// Accent colours (ARGB) offered in AJUSTES > Color de acento.
// San Andreas = the pale blue of the game's own front end ("Menú principal"); Grove green; HUD money green; gold; red; white.
inline const unsigned accentColors[] = {0xFFA8C0E6, 0xFF49A84A, 0xFF4E9A3E, 0xFFE2B649, 0xFFC8343C, 0xFFEDEDED};
enum MenuButton { MB_R1_DOWN, MB_L1_DOWN, MB_OPTIONS, MB_DOUBLE_DOWN };
enum OpenButton { OB_R1_DOWN, OB_L1_DOWN, OB_L3_R3, OB_DOUBLE_DOWN, OB_DOUBLE_RIGHT };   // opens/closes photo mode itself

inline std::vector<std::string> poseNames() { std::vector<std::string> n; for (const PoseDef& p : poseDefs) n.push_back(p.label); return n; }
inline std::vector<MenuItem> makeMenu() {
    std::vector<std::string> weather(std::begin(weatherNames), std::end(weatherNames));
    const std::vector<std::string> yesNo{"No", "Sí"};
    return {
        {IT_GRID, TAB_COMPOSE, KIND_CHOICE, "Rejilla", "Grid", 0, 4, 1, 0, {"No", "Tercios", "Cuadrícula", "Centro", "Diagonales"}, true},
        {IT_FRAME, TAB_COMPOSE, KIND_CHOICE, "Marco", "Frame", 0, 6, 1, 0, {"No", "2.39:1 cine", "1.85:1", "16:9", "4:3", "1:1", "4:5 vertical"}, true},
        {IT_HIDE_PLAYER, TAB_COMPOSE, KIND_CHOICE, "Ocultar a CJ", "HidePlayer", 0, 1, 1, 0, yesNo, true},
        {IT_HIDE_PEOPLE, TAB_COMPOSE, KIND_CHOICE, "Ocultar personas", "", 0, 1, 1, 0, yesNo, false},
        {IT_POSE, TAB_COMPOSE, KIND_CHOICE, "Pose de CJ", "", 0, static_cast<float>(POSE_COUNT - 1), 1, 0, poseNames(), false},
        {IT_POSE_FRAME, TAB_COMPOSE, KIND_RANGE, "Momento de la pose", "", 0, 100, 5, 50, {}, false},
        {IT_EXPRESSION, TAB_COMPOSE, KIND_CHOICE, "Expresión", "", 0, static_cast<float>(EXPRESSION_COUNT - 1), 1, 0, expressionNames(), false},
        {IT_LOOK_AT, TAB_COMPOSE, KIND_CHOICE, "Mirar a la cámara", "", 0, 1, 1, 0, yesNo, false},
        {IT_WEAPON, TAB_COMPOSE, KIND_CHOICE, "Arma en la mano", "", 0, static_cast<float>(WEAPON_CHOICE_COUNT - 1), 1, 0, weaponNames(), false},
        {IT_TIME, TAB_COMPOSE, KIND_RANGE, "Hora", "Time", -0.5f, 23.5f, 0.5f, -0.5f, {}, false},
        {IT_WEATHER, TAB_COMPOSE, KIND_CHOICE, "Clima", "Weather", 0, static_cast<float>(weather.size() - 1), 1, 0, weather, false},
        {IT_SLOWMO, TAB_COMPOSE, KIND_CHOICE, "Tiempo", "TimeScale", 0, 4, 1, 0, {"Congelado", "Lento 0.05x", "Lento 0.1x", "Lento 0.25x", "Lento 0.5x"}, false},
        {IT_NEXT_FRAME, TAB_COMPOSE, KIND_ACTION, "Siguiente fotograma", "", 0, 0, 0, 0, {}, false},
        {IT_LOGO, TAB_COMPOSE, KIND_CHOICE, "Logo", "Logo", 0, 0, 1, 0, {"No"}, true},
        {IT_LOGO_POS, TAB_COMPOSE, KIND_CHOICE, "Posición del logo", "LogoPosition", 0, 4, 1, 0, {"Abajo derecha", "Abajo izquierda", "Arriba derecha", "Arriba izquierda", "Centro"}, true},
        {IT_LOGO_SIZE, TAB_COMPOSE, KIND_RANGE, "Tamaño del logo", "LogoSize", 5, 60, 5, 20, {}, true},
        {IT_PRESET_SLOT, TAB_COMPOSE, KIND_CHOICE, "Preset", "PresetSlot", 0, 2, 1, 0, {"Custom 1", "Custom 2", "Custom 3"}, false},
        {IT_PRESET_SAVE, TAB_COMPOSE, KIND_ACTION, "Guardar preset", "", 0, 0, 0, 0, {}, false},
        {IT_PRESET_LOAD, TAB_COMPOSE, KIND_ACTION, "Cargar preset", "", 0, 0, 0, 0, {}, false},
        {IT_CAMERA_MODE, TAB_COMPOSE, KIND_CHOICE, "Cámara", "CameraMode", 0, 1, 1, 0, {"Libre", "Orbital"}, false},
        {IT_MOVE_SPEED, TAB_SETTINGS, KIND_RANGE, "Velocidad de cámara", "MoveSpeed", 1, 60, 1, 12, {}, false},
        {IT_LOOK_SPEED, TAB_SETTINGS, KIND_RANGE, "Sensibilidad", "LookSpeed", 20, 300, 10, 100, {}, false},
        {IT_INVERT_Y, TAB_SETTINGS, KIND_CHOICE, "Invertir eje Y", "InvertY", 0, 1, 1, 0, yesNo, false},
        {IT_MAX_DISTANCE, TAB_SETTINGS, KIND_RANGE, "Distancia máxima", "MaxDistance", 20, 500, 10, 160, {}, false},
        {IT_COLLISION, TAB_SETTINGS, KIND_CHOICE, "Colisión", "Collision", 0, 1, 1, 1, yesNo, false},
        {IT_OPEN_BUTTON, TAB_SETTINGS, KIND_CHOICE, "Abrir modo foto con", "OpenButton", 0, 4, 1, 0, {"R1 + abajo", "L1 + abajo", "L3 + R3 (mantener)", "Doble toque abajo", "Doble toque derecha"}, false},
        {IT_MENU_BUTTON, TAB_SETTINGS, KIND_CHOICE, "Abrir menú con", "MenuButton", 0, 3, 1, 2, {"R1 + abajo", "L1 + abajo", "Options", "Doble toque abajo"}, false},
        {IT_PROMPTS, TAB_SETTINGS, KIND_CHOICE, "Ayuda de controles", "Prompts", 0, 1, 1, 1, yesNo, false},
        {IT_STATUS, TAB_SETTINGS, KIND_CHOICE, "Mostrar estado", "Status", 0, 1, 1, 1, yesNo, false},
        {IT_ACCENT, TAB_SETTINGS, KIND_CHOICE, "Color de acento", "Accent", 0, 5, 1, 0, {"San Andreas", "Grove Street", "Dinero", "Dorado", "Rojo", "Blanco"}, false},
        {IT_MENU_BG, TAB_SETTINGS, KIND_CHOICE, "Fondo del menú", "MenuBackground", 0, 2, 1, 0, {"Translúcido", "Oscuro", "Opaco"}, false},
        {IT_ICONS, TAB_SETTINGS, KIND_CHOICE, "Íconos de botones", "ButtonIcons", 0, 2, 1, 0, {"Auto (GInput)", "PlayStation", "Xbox"}, false},
        {IT_SOUNDS, TAB_SETTINGS, KIND_CHOICE, "Sonidos", "Sounds", 0, 1, 1, 1, yesNo, false},
        {IT_MUSIC, TAB_SETTINGS, KIND_CHOICE, "Música", "Music", 0, 1, 1, 0, yesNo, false},
        {IT_MUSIC_VOLUME, TAB_SETTINGS, KIND_RANGE, "Volumen de música", "MusicVolume", 0, 100, 10, 60, {}, false},
        {IT_MOUSE_LOOK, TAB_SETTINGS, KIND_RANGE, "Sensibilidad del ratón", "MouseLook", 0, 100, 5, 40, {}, false},
        {IT_LANGUAGE, TAB_SETTINGS, KIND_CHOICE, "Idioma", "Language", 0, 2, 1, 0, {"Auto (juego)", "Español", "English"}, false},
        {IT_CUTSCENE_CAMERA, TAB_SETTINGS, KIND_CHOICE, "Cámara libre en cinemáticas", "CutsceneCamera", 0, 1, 1, 1, {"No", "Solo exteriores"}, false},
        // Where photos go: Pictures\GTA San Andreas Photo Mode, the game's Gallery folder (User Files\Gallery) or a Photos folder next to the mod.
        // PhotoFolder= in the mod's INI still wins over this.
        {IT_PHOTO_FOLDER, TAB_SETTINGS, KIND_CHOICE, "Carpeta de fotos", "SaveFolder", 0, 2, 1, 0, {"Imágenes", "Galería del juego", "Junto al mod"}, false},
        // Also writes a JPG copy named gallery1.jpg, gallery2.jpg... into User Files\Gallery, the files the game's (and SkyUI's) gallery shows.
        {IT_GALLERY_COPY, TAB_SETTINGS, KIND_CHOICE, "Copia JPG para la galería", "GalleryCopy", 0, 1, 1, 0, yesNo, false},
        {IT_SETTINGS_RESET, TAB_SETTINGS, KIND_ACTION, "Restablecer ajustes", "", 0, 0, 0, 0, {}, false},
        {IT_FILTER, TAB_FX, KIND_CHOICE, "Filtro", "Filter", 0, 7, 1, 0, {"Ninguno", "Blanco y negro", "Sepia", "Frío", "Cálido", "Vintage", "Polaroid", "Cámara de los 90"}, true},
        {IT_EXPOSURE, TAB_FX, KIND_RANGE, "Exposición", "Exposure", -2, 2, 0.1f, 0, {}, true},
        {IT_CONTRAST, TAB_FX, KIND_RANGE, "Contraste", "Contrast", -50, 50, 5, 0, {}, true},
        {IT_SATURATION, TAB_FX, KIND_RANGE, "Saturación", "Saturation", -100, 100, 5, 0, {}, true},
        {IT_TEMPERATURE, TAB_FX, KIND_RANGE, "Temperatura", "Temperature", -100, 100, 5, 0, {}, true},
        {IT_VIGNETTE, TAB_FX, KIND_RANGE, "Viñeta", "Vignette", 0, 100, 5, 0, {}, true},
        {IT_GRAIN, TAB_FX, KIND_RANGE, "Grano", "Grain", 0, 100, 5, 0, {}, true},
        {IT_TILT, TAB_FX, KIND_RANGE, "Desenfoque de bordes", "TiltShift", 0, 100, 5, 0, {}, true},
        {IT_TILT_FOCUS, TAB_FX, KIND_RANGE, "Altura del foco", "TiltFocus", 0, 100, 5, 50, {}, true},
        {IT_LENS_DROPS, TAB_FX, KIND_RANGE, "Gotas en la lente", "LensDrops", 0, 100, 5, 0, {}, true},
        // SkyGfx preset: choices filled at runtime from skygfx.asi; no key, so it is never saved (the game keeps its own preset)
        {IT_SKYGFX, TAB_FX, KIND_CHOICE, "Preset de SkyGfx", "", 0, 0, 1, 0, {"No disponible"}, false},
        {IT_CHROMA, TAB_FX, KIND_RANGE, "Aberración cromática", "Chroma", 0, 100, 5, 0, {}, true},
        {IT_BLOOM, TAB_FX, KIND_RANGE, "Resplandor", "Bloom", 0, 100, 5, 0, {}, true},
        {IT_DISTORTION, TAB_FX, KIND_RANGE, "Distorsión de lente", "Distortion", -50, 50, 5, 0, {}, true},
        {IT_RADIAL_BLUR, TAB_FX, KIND_RANGE, "Desenfoque radial", "RadialBlur", 0, 100, 5, 0, {}, true},
        {IT_HAZE, TAB_FX, KIND_RANGE, "Neblina", "Haze", 0, 100, 5, 0, {}, true},
        {IT_FISHEYE, TAB_FX, KIND_RANGE, "Ojo de pez", "Fisheye", 0, 100, 5, 0, {}, true},   // widens the camera and bends the image
        {IT_FISHEYE_CIRCLE, TAB_FX, KIND_CHOICE, "Borde circular", "FisheyeCircle", 0, 1, 1, 0, yesNo, true},
        {IT_FX_RESET, TAB_FX, KIND_ACTION, "Restablecer efectos", "", 0, 0, 0, 0, {}, false},
        {IT_LIGHT_SLOT, TAB_LIGHTS, KIND_CHOICE, "Luz", "", 0, 2, 1, 0, {"Luz 1", "Luz 2", "Luz 3"}, false},
        {IT_LIGHT_TYPE, TAB_LIGHTS, KIND_CHOICE, "Tipo", "", 0, 2, 1, 0, {"Foco redondo", "Reflector", "Flash de cámara"}, false},
        {IT_LIGHT_PLACE, TAB_LIGHTS, KIND_ACTION, "Colocar aquí", "", 0, 0, 0, 0, {}, false},
        {IT_LIGHT_MOVE, TAB_LIGHTS, KIND_ACTION, "Mover luz", "", 0, 0, 0, 0, {}, false},
        {IT_LIGHT_MOVE_MODE, TAB_LIGHTS, KIND_CHOICE, "Modo de movimiento", "", 0, 1, 1, 0, {"Libre", "Alrededor de CJ"}, false},
        {IT_LIGHT_ON, TAB_LIGHTS, KIND_CHOICE, "Encendida", "", 0, 1, 1, 0, yesNo, false},
        {IT_LIGHT_COLOR, TAB_LIGHTS, KIND_CHOICE, "Color", "", 0, static_cast<float>(LIGHT_COLOR_COUNT - 1), 1, 0, lightColorNames(), false},
        {IT_LIGHT_INTENSITY, TAB_LIGHTS, KIND_RANGE, "Intensidad", "", 5, 100, 5, 60, {}, false},
        {IT_LIGHT_RANGE, TAB_LIGHTS, KIND_RANGE, "Alcance", "", 2, 40, 1, 10, {}, false},
        {IT_LIGHT_HUE, TAB_LIGHTS, KIND_RANGE, "Tono", "", 0, 350, 10, 30, {}, false},   // changing it switches Color to Personalizado
        {IT_LIGHT_SAT, TAB_LIGHTS, KIND_RANGE, "Pureza del color", "", 0, 100, 5, 60, {}, false},
        {IT_LIGHT_SHADOWS, TAB_LIGHTS, KIND_CHOICE, "Sombras", "LightShadows", 0, 1, 1, 1, yesNo, false},   // with Proper Shaders only
        {IT_LIGHT_REMOVE, TAB_LIGHTS, KIND_ACTION, "Quitar esta luz", "", 0, 0, 0, 0, {}, false},
        {IT_LIGHT_CLEAR, TAB_LIGHTS, KIND_ACTION, "Quitar todas las luces", "", 0, 0, 0, 0, {}, false},
    };
}

struct MenuState {
    std::vector<MenuItem> items = makeMenu();
    float value[IT_COUNT]{};
    bool open = false; MenuTab tab = TAB_COMPOSE; int row = 0;
    MenuState() { for (const MenuItem& i : items) value[i.id] = i.def; }
    const MenuItem& item(ItemId id) const { return items[static_cast<size_t>(id)]; }
    MenuItem& item(ItemId id) { return items[static_cast<size_t>(id)]; }
    std::vector<ItemId> rows(MenuTab t) const { std::vector<ItemId> r; for (const MenuItem& i : items) if (i.tab == t) r.push_back(i.id); return r; }
    ItemId current() const { auto r = rows(tab); return r.empty() ? IT_COUNT : r[static_cast<size_t>(std::clamp(row, 0, static_cast<int>(r.size()) - 1))]; }
    int choice(ItemId id) const { return static_cast<int>(std::lround(value[id])); }
    bool on(ItemId id) const { return choice(id) != 0; }
    // Returns true when the value really changed (the "nochange" sound plays otherwise).
    bool adjust(ItemId id, int dir) {
        MenuItem& i = item(id); if (i.kind == KIND_ACTION || dir == 0) return false;
        float before = value[id];
        if (i.kind == KIND_CHOICE) {
            int n = static_cast<int>(i.max) + 1; if (n <= 1) return false;
            value[id] = static_cast<float>(((choice(id) + dir) % n + n) % n);      // choices wrap around
        } else {
            float v = std::round((value[id] + dir * i.step) / i.step) * i.step;  // snap to the step grid
            value[id] = std::clamp(v, i.min, i.max);
        }
        return value[id] != before;
    }
    void moveRow(int dir) { int n = static_cast<int>(rows(tab).size()); if (n) row = ((row + dir) % n + n) % n; }
    void moveTab(int dir) { tab = static_cast<MenuTab>(((tab + dir) % TAB_COUNT + TAB_COUNT) % TAB_COUNT); row = 0; }
    void setChoices(ItemId id, std::vector<std::string> c) {
        MenuItem& i = item(id); if (c.empty()) c.push_back("No");
        i.choices = std::move(c); i.max = static_cast<float>(i.choices.size() - 1);
        value[id] = std::clamp(value[id], 0.f, i.max);
    }
    void resetTab(MenuTab t) { for (const MenuItem& i : items) if (i.tab == t && i.kind != KIND_ACTION) value[i.id] = i.def; }
    void resetFx() { resetTab(TAB_FX); }
    std::string text(ItemId id) const {
        const MenuItem& i = item(id); char b[48];
        if (i.kind == KIND_ACTION) return "";
        if (i.kind == KIND_CHOICE) { int c = choice(id); return c >= 0 && c < static_cast<int>(i.choices.size()) ? i.choices[static_cast<size_t>(c)] : "?"; }
        if (id == IT_TIME) {
            if (value[id] < 0) return "Sin cambio";
            int minutes = static_cast<int>(std::lround(value[id] * 60)); std::snprintf(b, sizeof(b), "%02d:%02d", minutes / 60, minutes % 60); return b;
        }
        if (i.step < 1) std::snprintf(b, sizeof(b), "%+.1f", value[id]);
        else if (i.min < 0) std::snprintf(b, sizeof(b), "%+d", static_cast<int>(std::lround(value[id])));
        else std::snprintf(b, sizeof(b), "%d", static_cast<int>(std::lround(value[id])));
        return b;
    }
    // INI round trip: "Key=value" pairs; presets keep only items flagged as preset.
    std::vector<std::pair<std::string, std::string>> serialize(bool presetOnly) const {
        std::vector<std::pair<std::string, std::string>> out; char b[32];
        for (const MenuItem& i : items) {
            if (!*i.key || (presetOnly && !i.preset)) continue;
            if (i.id == IT_LOGO) { out.push_back({i.key, i.choices[static_cast<size_t>(choice(IT_LOGO))]}); continue; }   // logos stored by file name
            std::snprintf(b, sizeof(b), "%g", value[i.id]); out.push_back({i.key, b});
        }
        return out;
    }
    void load(const char* key, const char* text) {
        for (MenuItem& i : items) {
            if (!*i.key || std::strcmp(i.key, key) != 0) continue;
            if (i.id == IT_LOGO) { for (size_t c = 0; c < i.choices.size(); ++c) if (i.choices[c] == text) value[i.id] = static_cast<float>(c); return; }
            char* end = nullptr; float v = std::strtof(text, &end);
            if (end != text && std::isfinite(v)) value[i.id] = i.kind == KIND_RANGE ? std::clamp(v, i.min, i.max) : std::clamp(std::round(v), 0.f, i.max);
            return;
        }
    }
};
// Letterbox: bar sizes that keep a target aspect inside a w x h image (0 aspect = no bars).
struct Bars { float top, side; };
inline Bars letterbox(float w, float h, float aspect) {
    if (aspect <= 0 || w <= 0 || h <= 0) return {0, 0};
    float current = w / h;
    if (aspect > current) return {(h - w / aspect) * 0.5f, 0};
    return {0, (w - h * aspect) * 0.5f};
}
// Logo rectangle: width as a percentage of the image width, kept inside a margin, aspect preserved.
struct Box { float x, y, w, h; };
inline Box logoBox(float imgW, float imgH, float logoW, float logoH, float sizePct, int position, Bars bars) {
    float w = imgW * sizePct / 100.f, h = logoH > 0 ? w * logoH / logoW : 0, m = imgH * 0.03f;
    float left = bars.side + m, right = imgW - bars.side - m - w, top = bars.top + m, bottom = imgH - bars.top - m - h;
    switch (position) {
    case 1: return {left, bottom, w, h};
    case 2: return {right, top, w, h};
    case 3: return {left, top, w, h};
    case 4: return {(imgW - w) * 0.5f, (imgH - h) * 0.5f, w, h};
    default: return {right, bottom, w, h};
    }
}

// ---------- translations ----------
// The menu is written in Spanish; English comes from this table. tr() returns the input when it has no entry
// (numbers, file names). test_core checks every label and choice has an entry.
struct Translation { const char* es; const char* en; };
inline const Translation translations[] = {
    {"COMPONER","COMPOSE"},{"AJUSTES","SETTINGS"},{"EFECTOS","PHOTO FX"},{"FOTOS","PHOTOS"},{"LUCES","LIGHTS"},{"Expresión","Expression"},{"Sorprendido","Surprised"},{"Asustado","Scared"},{"Hablando","Talking"},{"Chicle","Chewing gum"},
    {"Curioso","Curious"},{"Arma en la mano","Weapon in hand"},{"Katana","Katana"},{"Desert Eagle","Desert Eagle"},{"Uzi","Uzi"},{"Tec-9","Tec-9"},{"MP5","MP5"},{"AK-47","AK-47"},{"M4","M4"},{"RPG","RPG"},{"Minigun","Minigun"},{"Sin cambio","Unchanged"},{"Ninguna","None"},{"Bate","Bat"},{"Cuchillo","Knife"},{"Motosierra","Chainsaw"},{"Bastón","Cane"},{"Flores","Flowers"},{"Pistola","Pistol"},{"Pistola con silenciador","Silenced pistol"},{"Escopeta","Shotgun"},{"Recortada","Sawn-off"},{"Escopeta de combate","Combat shotgun"},{"Rifle","Rifle"},{"Francotirador","Sniper rifle"},{"Lanzallamas","Flamethrower"},{"Aerosol","Spray can"},{"Cámara de fotos","Camera"},{"Mirar a la cámara","Look at camera"},{"Cámara","Camera"},{"Libre","Free"},{"Orbital","Orbit"},{"Ocultar personas","Hide people"},{"Distorsión de lente","Lens distortion"},{"Desenfoque radial","Radial blur"},{"Neblina","Haze"},
    {"Tipo","Type"},{"Foco redondo","Point light"},{"Reflector","Spotlight"},{"Flash de cámara","Camera flash"},
    {"Siguiente fotograma","Next frame"},{"Cámara libre en cinemáticas","Free camera in cutscenes"},{"Solo exteriores","Outdoors only"},
    {"Aberración cromática","Chromatic aberration"},{"Resplandor","Bloom"},
    {"Luz","Light"},{"Luz 1","Light 1"},{"Luz 2","Light 2"},{"Luz 3","Light 3"},{"Colocar aquí","Place here"},{"Encendida","On"},{"Color","Colour"},
    {"Intensidad","Intensity"},{"Alcance","Range"},{"Quitar todas las luces","Remove all lights"},
    {"Blanco cálido","Warm white"},{"Personalizado","Custom"},{"Tono","Hue"},{"Pureza del color","Colour purity"},
    {"Carpeta de fotos","Photo folder"},{"Imágenes","Pictures"},{"Galería del juego","Game gallery"},{"Junto al mod","Next to the mod"},{"Copia JPG para la galería","JPG copy for the gallery"},
    {"Ojo de pez","Fisheye"},{"Borde circular","Round border"},{"Blanco frío","Cool white"},{"Azul","Blue"},{"Verde","Green"},{"Naranja","Orange"},{"Morado","Purple"},{"Rosa","Pink"},{"Amarillo","Yellow"},
    {"Luz colocada","Light placed"},{"Las poses necesitan Tiempo: Congelado","Poses need Time: Frozen"},{"Mover luz","Move light"},{"Modo de movimiento","Move mode"},{"Alrededor de CJ","Around CJ"},{"Quitar esta luz","Remove this light"},{"Sombras","Shadows"},{"Luz quitada","Light removed"},{"Mueve la luz con el stick o WASD, gírala con la cámara. X / Enter la suelta","Move the light with the stick or WASD, aim it with the look controls. X / Enter drops it"},{"Pulsa Derecha","Press Right"},{"Moviendo","Moving"},{"Luces quitadas","Lights removed"},{"Primero coloca la luz","Place the light first"},
    {"Rejilla","Grid"},{"Tercios","Thirds"},{"Cuadrícula","Squares"},{"Centro","Centre"},{"Diagonales","Diagonals"},
    {"Marco","Frame"},{"2.39:1 cine","2.39:1 cinema"},{"1.85:1","1.85:1"},{"16:9","16:9"},{"4:3","4:3"},{"1:1","1:1"},{"4:5 vertical","4:5 portrait"},
    {"Ocultar a CJ","Hide CJ"},{"No","Off"},{"Sí","On"},{"Hora","Time of day"},{"Sin cambio","Unchanged"},
    {"Clima","Weather"},{"Tiempo","Time"},{"Congelado","Frozen"},{"Lento 0.05x","Slow 0.05x"},{"Lento 0.1x","Slow 0.1x"},{"Lento 0.25x","Slow 0.25x"},{"Lento 0.5x","Slow 0.5x"},
    {"Logo","Logo"},{"Posición del logo","Logo position"},{"Abajo derecha","Bottom right"},{"Abajo izquierda","Bottom left"},{"Arriba derecha","Top right"},{"Arriba izquierda","Top left"},
    {"Tamaño del logo","Logo size"},{"Preset","Preset"},{"Guardar preset","Save preset"},{"Cargar preset","Load preset"},
    {"Velocidad de cámara","Camera speed"},{"Sensibilidad","Look sensitivity"},{"Invertir eje Y","Invert Y axis"},{"Distancia máxima","Max distance"},{"Colisión","Collision"},
    {"Abrir modo foto con","Open photo mode with"},{"R1 + abajo","R1 + down"},{"L1 + abajo","L1 + down"},{"L3 + R3 (mantener)","L3 + R3 (hold)"},{"Doble toque abajo","Double tap down"},{"Doble toque derecha","Double tap right"},
    {"Abrir menú con","Open menu with"},{"Options","Options"},
    {"Ayuda de controles","Button prompts"},{"Mostrar estado","Show status"},{"Color de acento","Accent colour"},{"San Andreas","San Andreas"},{"Grove Street","Grove Street"},{"Dinero","Money"},{"Dorado","Gold"},{"Rojo","Red"},{"Blanco","White"},
    {"Fondo del menú","Menu background"},{"Translúcido","Translucent"},{"Oscuro","Dark"},{"Opaco","Opaque"},
    {"Íconos de botones","Button icons"},{"Auto (GInput)","Auto (GInput)"},{"PlayStation","PlayStation"},{"Xbox","Xbox"},
    {"Sonidos","Sounds"},{"Música","Music"},{"Volumen de música","Music volume"},{"Sensibilidad del ratón","Mouse sensitivity"},
    {"Idioma","Language"},{"Auto (juego)","Auto (game)"},{"Español","Español"},{"English","English"},{"Restablecer ajustes","Reset settings"},
    {"Filtro","Filter"},{"Ninguno","None"},{"Blanco y negro","Black & white"},{"Sepia","Sepia"},{"Frío","Cold"},{"Cálido","Warm"},{"Vintage","Vintage"},{"Polaroid","Polaroid"},{"Cámara de los 90","90s camcorder"},{"Preset de SkyGfx","SkyGfx preset"},{"No disponible","Not available"},
    {"Exposición","Exposure"},{"Contraste","Contrast"},{"Saturación","Saturation"},{"Temperatura","Temperature"},{"Viñeta","Vignette"},{"Grano","Grain"},
    {"Desenfoque de bordes","Edge blur"},{"Altura del foco","Focus height"},{"Gotas en la lente","Lens drops"},{"Restablecer efectos","Reset effects"},
    {"Muy soleado LS","Extra sunny LS"},{"Soleado LS","Sunny LS"},{"Smog LS","Smog LS"},{"Soleado con smog LS","Sunny smog LS"},{"Nublado LS","Cloudy LS"},
    {"Soleado SF","Sunny SF"},{"Muy soleado SF","Extra sunny SF"},{"Nublado SF","Cloudy SF"},{"Lluvia SF","Rain SF"},{"Niebla SF","Fog SF"},
    {"Soleado LV","Sunny LV"},{"Muy soleado LV","Extra sunny LV"},{"Nublado LV","Cloudy LV"},{"Muy soleado campo","Extra sunny countryside"},{"Soleado campo","Sunny countryside"},
    {"Nublado campo","Cloudy countryside"},{"Lluvia campo","Rain countryside"},{"Muy soleado desierto","Extra sunny desert"},{"Soleado desierto","Sunny desert"},
    {"Tormenta de arena","Sandstorm"},{"Bajo el agua","Underwater"},{"Color extra 1","Extra colour 1"},{"Color extra 2","Extra colour 2"},
    {"Custom 1","Custom 1"},
    {"Pose de CJ","CJ pose"},{"Momento de la pose","Pose moment"},{"Normal","Normal"},{"Caminando (pandilla)","Walking (gang)"},{"Caminando","Walking"},
    {"Corriendo (pandilla)","Running (gang)"},{"Sprint","Sprint"},{"Saltando","Jumping"},{"Cayendo","Falling"},{"Cansado","Tired"},{"Manos arriba","Hands up"},
    {"Hablando por teléfono","On the phone"},{"Rascándose","Scratching"},{"Seña de pandilla 1","Gang sign 1"},{"Seña de pandilla 2","Gang sign 2"},
    {"Seña de pandilla 3","Gang sign 3"},{"Platicando","Chatting"},{"Recargado","Leaning"},{"Fumando","Smoking"},{"Fumando recargado","Smoking, leaning"},
    {"Sentado","Sitting"},{"Acostado","Lying down"},{"Bailando 1","Dancing 1"},{"Bailando 2","Dancing 2"},{"Rapeando","Rapping"},{"Apuntando","Aiming"},
    {"Tomando foto","Taking a photo"},{"Señalando","Pointing"},{"Saludando","Waving"},{"Gritando","Shouting"},{"Tai chi","Tai chi"},{"Enojado","Angry"},
    {"Estirándose","Stretching"},{"Tirado","Knocked out"},{"Apuntando con pistola","Aiming a pistol"},{"Pistola a dos manos","Pistol, two hands"},{"Pistola agachado","Pistol, crouched"},{"Desert Eagle disparando","Desert Eagle firing"},{"Desert Eagle agachado","Desert Eagle, crouched"},{"Subfusil disparando","SMG firing"},{"Rifle disparando","Rifle firing"},{"Rifle agachado","Rifle, crouched"},{"Esa pose no está disponible","That pose is not available"},{"Custom 2","Custom 2"},{"Custom 3","Custom 3"},
    // interface
    {"Modo foto","Photo Mode"},{"Pestañas","Tabs"},{"Ajustar","Adjust"},{"Ocultar","Hide"},{"Cerrar","Close"},{"Elegir","Browse"},{"Ver","View"},{"Borrar","Delete"},{"Carpeta","Folder"},
    {"Pulsa X","Press X"},{"Pulsa Enter","Press Enter"},{"Todavía no hay fotos. Toma una con X.","No photos yet. Take one with X."},
    {"Pulsa □ otra vez para enviar a la papelera","Press □ again to move it to the Recycle Bin"},
    {"Menú","Menu"},{"Menú (2 toques)","Menu (2 taps)"},{"Foto","Photo"},{"Mover / mirar","Move / look"},{"Mover","Move"},{"Mirar","Look"},{"Bajar / subir","Down / up"},{"Inclinar","Tilt"},{"Velocidad","Speed"},{"Zoom (FOV)","Zoom (FOV)"},
    {"Reiniciar cámara","Reset camera"},{"Ocultar interfaz","Hide interface"},{"Salir (mantener)","Exit (hold)"},{"Salir","Exit"},{"Cerrar modo foto","Close photo mode"},{"Más rápido","Faster"},
    {"Anterior / siguiente","Previous / next"},{"Volver","Back"},{"Cámara lenta","Slow motion"},
    {"Foto guardada","Photo saved"},{"No se pudo guardar la foto","Could not save the photo"},{"Foto enviada a la papelera","Photo moved to the Recycle Bin"},{"No se pudo borrar la foto","Could not delete the photo"},
    {"Efectos restablecidos","Effects reset"},{"Ajustes restablecidos","Settings reset"},{"Carpeta de fotos abierta","Photo folder opened"},{"Cámara reiniciada","Camera reset"},
    {"Ese preset está vacío","That preset is empty"},{"Guardado en Custom %d","Saved to Custom %d"},{"Custom %d cargado","Custom %d loaded"},
    {"página %d de %d","page %d of %d"},{"ratón","mouse"},
    {"Espacio","Space"},{"Clic","Click"},{"Ratón","Mouse"},{"Flechas","Arrows"},{"Supr","Del"},
};
enum Language { LANG_ES, LANG_EN };
inline const char* tr(const char* es, int lang) {
    if (lang == LANG_ES || !es) return es;
    for (const Translation& t : translations) if (std::strcmp(t.es, es) == 0) return t.en;
    return es;
}
inline bool hasTranslation(const char* es) { for (const Translation& t : translations) if (std::strcmp(t.es, es) == 0) return true; return false; }
// SA's own language preference (CMenuManager::m_nPrefsLanguage): 4 = Spanish; everything else falls back to English.
inline int resolveLanguage(int setting, int gameLanguage) { return setting == 1 ? LANG_ES : setting == 2 ? LANG_EN : gameLanguage == 4 ? LANG_ES : LANG_EN; }
