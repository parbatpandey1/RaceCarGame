#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <random>

using namespace sf;
using namespace std;

// Window dimensions
const int WIDTH = 1024;
const int HEIGHT = 768;

// Road parameters
const int ROAD_W = 2500;  // Increased road width (was 2000)
const int SEG_LEN = 200;   // Segment length
const float CAM_D = 0.84f;  // Camera depth

const int NUM_LANES = 3;

// Game states
enum GameState { MENU, PLAYING, GAME_OVER_PROMPT };

// Draw one segment of road as a quad
void drawQuad(RenderWindow& w, Color c, int x1, int y1, int w1, int x2, int y2, int w2) {
    ConvexShape shape(4);
    shape.setFillColor(c);
    shape.setPoint(0, Vector2f(float(x1 - w1), float(y1)));
    shape.setPoint(1, Vector2f(float(x2 - w2), float(y2)));
    shape.setPoint(2, Vector2f(float(x2 + w2), float(y2)));
    shape.setPoint(3, Vector2f(float(x1 + w1), float(y1)));
    w.draw(shape);
}

// One line/segment of the road
struct Line {
    float x = 0, y = 0, z = 0;       // 3D world coordinates
    float X = 0, Y = 0, W = 0;       // screen coordinates
    float clip = 0, scale = 0;
    float curve = 0;                 // curve value for this segment
    Sprite opCar;                    // opponent car sprite
    bool hasOpponent = false;
    int opponentLane = 1;            // Which lane (0, 1, 2) the opponent is in
    float opponentOffset = 0;        // Offset within the lane for variety

    // Scenery objects
    Sprite scenerySprite;
    bool hasScenery = false;
    int sceneryType = 0;             // 0=palm1, 1=palm2, 2=house, 3=grass
    bool sceneryOnLeft = true;       // true=left side, false=right side

    void project(int camX, int camY, int camZ) {
        scale = CAM_D / (z - camZ);
        X = (1 + scale * (x - camX)) * WIDTH / 2;
        Y = (1 - scale * (y - camY)) * HEIGHT / 2;
        W = scale * ROAD_W * WIDTH / 2;
    }

    FloatRect drawOpponent(RenderWindow& win, int playerZ) {
        // Early outs
        if (!hasOpponent || !opCar.getTexture()) return FloatRect();

        Sprite s = opCar;
        IntRect rt = s.getTextureRect();
        float originalWidth = (rt.width > 0) ? float(rt.width) : float(s.getTexture()->getSize().x);
        float originalHeight = (rt.height > 0) ? float(rt.height) : float(s.getTexture()->getSize().y);
        if (originalWidth <= 0 || originalHeight <= 0) return FloatRect();

        // Distance in world units from the player to this segment
        float dz = z - playerZ;
        if (dz <= 0) return FloatRect(); // behind or at player

        // --- WIDTH / SCALE DETERMINATION ---
        // Use the projected road half-width (W) to get a lane pixel width. This automatically
        // encodes perspective (far segments have smaller W, near segments have larger W).
        float laneWidth = (W * 2.0f) / NUM_LANES; // pixels

        // Base fraction of the lane that a car occupies (tweakable)
        const float LANE_CAR_FRACTION = 0.55f; // 55% of lane width by default
        float destW = laneWidth * LANE_CAR_FRACTION;

        // Add a small "close-up" boost so cars feel noticeably bigger when they're very near
        const float CLOSE_BOOST_RANGE = SEG_LEN * 6; // within ~6 segments we start boosting size
        if (dz < CLOSE_BOOST_RANGE) {
            float t = (CLOSE_BOOST_RANGE - dz) / CLOSE_BOOST_RANGE; // 0..1
            const float CLOSE_MAX_BOOST = 0.45f; // up to +45% size
            destW *= (1.0f + t * CLOSE_MAX_BOOST);
        }

        // Keep sizes within reasonable screen bounds
        destW = max(8.0f, min(destW, WIDTH * 0.9f));

        // Preserve aspect ratio
        float destH = destW * (originalHeight / originalWidth);
        destH = max(6.0f, min(destH, HEIGHT * 0.9f));

        // --- POSITIONING (use projected X, Y and lane offsets) ---
        float laneStart = -W + laneWidth * opponentLane;
        float laneCenter = laneStart + laneWidth * 0.5f + opponentOffset * laneWidth * 0.25f; // small lateral offset

        float carX = X + laneCenter - destW * 0.5f;
        // Place the bottom of the sprite exactly on the road (Y is road surface in projection)
        float carY = Y - destH;

        // Cull if completely off-screen
        if (carY > HEIGHT + 200 || carY + destH < -200 || carX + destW < -200 || carX > WIDTH + 200) {
            return FloatRect();
        }

        // --- SHADOW ---
        if (destW > 8.0f) {
            float shadowW = destW * 0.78f;
            float shadowH = max(3.0f, destW * 0.06f);
            RectangleShape shadow(Vector2f(shadowW, shadowH));
            // Center shadow under the car, slightly above the projected road to simulate contact
            shadow.setOrigin(shadowW * 0.5f, shadowH * 0.5f);
            shadow.setPosition(carX + destW * 0.5f, Y - shadowH * 0.5f + 4.0f);

            // Shadow alpha stronger when closer
            float alpha = 60.0f + (1.0f - min(dz / (SEG_LEN * 12.0f), 1.0f)) * 140.0f; // between ~60 and ~200
            if (alpha > 200.0f) alpha = 200.0f;
            shadow.setFillColor(Color(0, 0, 0, static_cast<Uint8>(alpha)));
            win.draw(shadow);
        }

        // --- DRAW CAR ---
        s.setTextureRect(IntRect(0, 0, int(originalWidth), int(originalHeight)));
        s.setScale(destW / originalWidth, destH / originalHeight);
        s.setPosition(carX, carY);
        win.draw(s);

        return FloatRect(carX, carY, destW, destH);
    }




    void drawScenery(RenderWindow& win, int playerZ) {
        if (!hasScenery || !scenerySprite.getTexture()) return;

        // Calculate distance-based scale - MAXIMUM VISIBILITY RANGE
        float distance = abs(z - playerZ);

        // MAXIMUM: Objects visible from VERY far away for ultra-smooth appearance
        float scale;

        if (distance > SEG_LEN * 120) {
            return; // Too far to see - GREATLY EXTENDED from 80 to 120 segments
        }
        else if (distance > SEG_LEN * 100) {
            // Far horizon: barely visible dots (0.02 to 0.04)
            float t = (SEG_LEN * 120 - distance) / (SEG_LEN * 20);
            scale = 0.02f + t * 0.02f;
        }
        else if (distance > SEG_LEN * 80) {
            // Horizon: tiny but visible dots (0.04 to 0.06)
            float t = (SEG_LEN * 100 - distance) / (SEG_LEN * 20);
            scale = 0.04f + t * 0.02f;
        }
        else if (distance > SEG_LEN * 60) {
            // Very very far: small specks (0.06 to 0.09)
            float t = (SEG_LEN * 80 - distance) / (SEG_LEN * 20);
            scale = 0.06f + t * 0.03f;
        }
        else if (distance > SEG_LEN * 45) {
            // Very far: becoming noticeable (0.09 to 0.14)
            float t = (SEG_LEN * 60 - distance) / (SEG_LEN * 15);
            scale = 0.09f + t * 0.05f;
        }
        else if (distance > SEG_LEN * 30) {
            // Far: clearly visible (0.14 to 0.22)
            float t = (SEG_LEN * 45 - distance) / (SEG_LEN * 15);
            scale = 0.14f + t * 0.08f;
        }
        else if (distance > SEG_LEN * 20) {
            // Medium-far: good size (0.22 to 0.35)
            float t = (SEG_LEN * 30 - distance) / (SEG_LEN * 10);
            scale = 0.22f + t * 0.13f;
        }
        else if (distance > SEG_LEN * 12) {
            // Medium: prominent (0.35 to 0.55)
            float t = (SEG_LEN * 20 - distance) / (SEG_LEN * 8);
            scale = 0.35f + t * 0.2f;
        }
        else if (distance > SEG_LEN * 6) {
            // Close: large and impressive (0.55 to 0.85)
            float t = (SEG_LEN * 12 - distance) / (SEG_LEN * 6);
            scale = 0.55f + t * 0.3f;
        }
        else if (distance > SEG_LEN * 3) {
            // Very close: dramatic size (0.85 to 1.3)
            float t = (SEG_LEN * 6 - distance) / (SEG_LEN * 3);
            scale = 0.85f + t * 0.45f;
        }
        else if (distance > SEG_LEN * 1) {
            // Extremely close: maximum size (1.3 to 1.8)
            float t = (SEG_LEN * 3 - distance) / (SEG_LEN * 2);
            scale = 1.3f + t * 0.5f;
        }
        else {
            // Right next to car: full size but reasonable (1.8 to 2.2)
            float t = (SEG_LEN * 1 - distance) / (SEG_LEN * 1);
            scale = 1.8f + t * 0.4f;
        }

        // Make grass smaller than other scenery
        if (sceneryType == 3) { // grass
            scale *= 0.6f; // Make grass 60% of normal size
        }

        // Get original texture size
        auto textureSize = scenerySprite.getTexture()->getSize();
        float originalWidth = float(textureSize.x);
        float originalHeight = float(textureSize.y);

        // Calculate scaled size
        float destW = originalWidth * scale;
        float destH = originalHeight * scale;

        // Position based on scenery type - different positioning for natural look
        float sideOffset;
        if (sceneryType == 2) { // House - always on right side
            sideOffset = W + destW * 0.5f + 200; // Right side only
        }
        else if (sceneryType == 3) { // Grass - always on left side  
            float grassDistance = 40 + (abs(opponentOffset) * 60); // 40-100 units from road edge
            sideOffset = -(W + destW * 0.5f + grassDistance); // Left side only
        }
        else { // Palm trees - vary distance from road for natural randomness
            float treeDistance = 60 + (abs(opponentOffset) * 80); // 60-140 units from road edge
            sideOffset = sceneryOnLeft ?
                -(W + destW * 0.5f + treeDistance) :
                (W + destW * 0.5f + treeDistance);
        }

        float sceneryX = X + sideOffset;
        float sceneryY = Y - destH; // Sit on ground level

        // ULTRA generous screen bounds to catch very distant objects
        if (sceneryY > HEIGHT + 300 || sceneryY + destH < -150 ||
            sceneryX + destW < -300 || sceneryX > WIDTH + 300 || destW < 1.5f) { // Very small minimum for maximum distance
            return;
        }

        // Set up and draw scenery
        Sprite s = scenerySprite;
        s.setScale(destW / originalWidth, destH / originalHeight);
        s.setPosition(sceneryX, sceneryY);
        win.draw(s);
    }
};

// Display the main menu
bool showMainMenu(RenderWindow& window) {
    Font font;
    if (!font.loadFromFile("fonts/OpenSans.ttf")) {
        // Try alternative path
        if (!font.loadFromFile("Fonts/OpenSans.ttf")) {
            return true; // Skip menu if font not found
        }
    }

    Text title("CAR RACING GAME", font, 60);
    title.setFillColor(Color::Yellow);
    title.setPosition(WIDTH / 2 - title.getGlobalBounds().width / 2, 100);

    Text instruction("Use Arrow Keys to Drive", font, 25);
    instruction.setFillColor(Color::Cyan);
    instruction.setPosition(WIDTH / 2 - instruction.getGlobalBounds().width / 2, 200);

    Text boost("Press SPACE for Boost!", font, 25);
    boost.setFillColor(Color::Magenta);
    boost.setPosition(WIDTH / 2 - boost.getGlobalBounds().width / 2, 240);

    vector<Text> options(2);
    options[0].setString("Start Game");
    options[1].setString("Exit");

    for (int i = 0; i < 2; i++) {
        options[i].setFont(font);
        options[i].setCharacterSize(40);
        options[i].setPosition(WIDTH / 2 - options[i].getGlobalBounds().width / 2, 350 + i * 70);
        options[i].setFillColor(i == 0 ? Color::Red : Color::White);
    }

    int selected = 0;
    Clock clock;

    while (window.isOpen()) {
        Event e;
        while (window.pollEvent(e)) {
            if (e.type == Event::Closed) return false;
            if (e.type == Event::KeyPressed) {
                if (e.key.code == Keyboard::Up || e.key.code == Keyboard::W) {
                    options[selected].setFillColor(Color::White);
                    selected = (selected - 1 + options.size()) % options.size();
                    options[selected].setFillColor(Color::Red);
                }
                else if (e.key.code == Keyboard::Down || e.key.code == Keyboard::S) {
                    options[selected].setFillColor(Color::White);
                    selected = (selected + 1) % options.size();
                    options[selected].setFillColor(Color::Red);
                }
                else if (e.key.code == Keyboard::Enter || e.key.code == Keyboard::Space) {
                    return selected == 0;
                }
            }
        }

        // Animate title
        float time = clock.getElapsedTime().asSeconds();
        title.setScale(1.0f + sin(time * 2) * 0.05f, 1.0f + sin(time * 2) * 0.05f);

        window.clear(Color(20, 20, 40));
        window.draw(title);
        window.draw(instruction);
        window.draw(boost);
        for (auto& opt : options) window.draw(opt);
        window.display();
    }
    return false;
}

int main() {
    mt19937 rng((unsigned)time(nullptr));
    uniform_int_distribution<int> dist_lane(0, NUM_LANES - 1);
    uniform_int_distribution<int> dist_car_type(0, 1);
    uniform_int_distribution<int> dist_spawn(0, 100);
    uniform_real_distribution<float> dist_offset(-0.8f, 0.8f);
    uniform_int_distribution<int> dist_scenery_type(0, 3);    // 4 scenery types now (0,1,2,3)
    uniform_int_distribution<int> dist_side(0, 1);           // left or right side
    uniform_int_distribution<int> dist_weighted_scenery(0, 9); // For weighted selection

    RenderWindow window(VideoMode(WIDTH, HEIGHT), "Car Race", Style::Default);
    window.setFramerateLimit(60);

    if (!showMainMenu(window)) return 0;

    // Load sounds (with error checking but continue if not found)
    SoundBuffer bufEngine, bufOver, bufBoost;
    Sound engine, sfxOver, sfxBoost;

    bool soundEnabled = true;
    if (!bufEngine.loadFromFile("sounds/sound.wav")) {
        cerr << "Warning: sound.wav not found" << endl;
        soundEnabled = false;
    }
    if (!bufOver.loadFromFile("sounds/game_over.wav")) {
        cerr << "Warning: game_over.wav not found" << endl;
    }
    if (!bufBoost.loadFromFile("sounds/boost.wav")) {
        cerr << "Warning: boost.wav not found" << endl;
    }

    if (soundEnabled) {
        engine.setBuffer(bufEngine);
        engine.setLoop(true);
        engine.play();
    }
    sfxOver.setBuffer(bufOver);
    sfxBoost.setBuffer(bufBoost);

    // Load fonts
    Font fontMain, fontScore;
    if (!fontMain.loadFromFile("Fonts/raider.ttf")) {
        if (!fontMain.loadFromFile("fonts/OpenSans.ttf")) {
            if (!fontMain.loadFromFile("Fonts/OpenSans.ttf")) {
                cerr << "Warning: Could not load fonts" << endl;
            }
        }
    }
    fontScore = fontMain;

    Text tGameOver("GAME OVER", fontMain, 80);
    tGameOver.setFillColor(Color(255, 50, 50));
    tGameOver.setOutlineColor(Color::White);
    tGameOver.setOutlineThickness(3);
    tGameOver.setPosition(WIDTH / 2 - tGameOver.getGlobalBounds().width / 2, HEIGHT / 2 - 150);

    Text tPrompt("Play Again? (Y/N)", fontScore, 40);
    tPrompt.setFillColor(Color::White);
    tPrompt.setPosition(WIDTH / 2 - tPrompt.getGlobalBounds().width / 2, HEIGHT / 2);

    Text tScore("", fontScore, 25);
    tScore.setFillColor(Color::Yellow);
    tScore.setOutlineColor(Color::Black);
    tScore.setOutlineThickness(2);
    tScore.setPosition(10, 10);

    Text tSpeed("", fontScore, 25);
    tSpeed.setFillColor(Color::Cyan);
    tSpeed.setOutlineColor(Color::Black);
    tSpeed.setOutlineThickness(2);
    tSpeed.setPosition(10, 40);

    // Text for remaining boosts count
    Text tBoostCount("", fontScore, 30);
    tBoostCount.setFillColor(Color::Red);
    tBoostCount.setOutlineColor(Color::Black);
    tBoostCount.setOutlineThickness(2);

    // Load background - PANORAMIC VERSION
    Texture bgTex;
    Sprite background;
    if (bgTex.loadFromFile("images/bg4.png")) {
        bgTex.setRepeated(false);  // CHANGED: disable repeating
        background.setTexture(bgTex);
        // Show more background (sky area) - increased from half to 60%
        int skyHeight = HEIGHT * 0.6;  // Use upper 60% of screen for background
        background.setTextureRect(IntRect(0, 0, WIDTH, skyHeight));
        background.setPosition(0, 0);  // Position at top of screen
    }

    // Load booster UI textures
    Texture boosterIconTex, boosterTextTex;
    Sprite boosterIcon, boosterText;
    bool hasBoosterUI = false;

    if (boosterIconTex.loadFromFile("images/boostericon.png") &&
        boosterTextTex.loadFromFile("images/boostertext.png")) {
        boosterIcon.setTexture(boosterIconTex);
        boosterText.setTexture(boosterTextTex);
        hasBoosterUI = true;
        cout << "Booster UI textures loaded successfully" << endl;
    }
    else {
        cerr << "Warning: Booster UI textures not found" << endl;
    }

    // Player car
    Texture playerCarTex;
    if (!playerCarTex.loadFromFile("images/car.png")) {
        cerr << "Warning: car.png not found" << endl;
    }
    RectangleShape player(Vector2f(120, 90));
    player.setTexture(&playerCarTex);
    player.setOrigin(60, 45);

    // Opponent car textures
    vector<Texture> opponentTextures(2);
    bool hasOpponentTextures = false;

    if (opponentTextures[0].loadFromFile("images/8.png") &&
        opponentTextures[1].loadFromFile("images/2nd.png")) {
        hasOpponentTextures = true;
    }
    else {
        cerr << "Warning: Opponent car textures not found" << endl;
        // Use player texture as fallback
        opponentTextures[0] = playerCarTex;
        opponentTextures[1] = playerCarTex;
        hasOpponentTextures = true;
    }

    // Load scenery textures - NOW INCLUDING GRASS (image 6)
    vector<Texture> sceneryTextures(4); // Increased from 3 to 4
    bool hasSceneryTextures = false;

    if (sceneryTextures[0].loadFromFile("images/4.png") &&    // Palm tree 1
        sceneryTextures[1].loadFromFile("images/5.png") &&    // Palm tree 2
        sceneryTextures[2].loadFromFile("images/7.png") &&    // House
        sceneryTextures[3].loadFromFile("images/6.png")) {    // Grass
        hasSceneryTextures = true;
        cout << "Scenery textures loaded successfully" << endl;
    }
    else {
        cerr << "Warning: Scenery textures not found" << endl;
    }

    // Create road segments
    vector<Line> lines;
    const int N = 1600;  // Total segments
    lines.resize(N);

    // Initialize road with curves and hills
    for (int i = 0; i < N; i++) {
        Line& line = lines[i];
        line.z = i * SEG_LEN;

        // Reduced curves to make road more natural
        if (i > 300 && i < 700) line.curve = 0.2f;  // Reduced from 0.5f
        if (i > 1100) line.curve = -0.3f;           // Reduced from -0.7f

        // Reduced hills for smoother road
        if (i > 750 && i < 1000) {
            line.y = sin((i - 750) * 0.02f) * 800;  // Reduced from 0.025f * 1500
        }
    }

    // Place opponent cars with very low density, appearing more after certain progress
    int opponentCount = 0;
    for (int i = 400; i < N; i += 150 + dist_spawn(rng) % 200) {  // Much wider spacing, start later
        if (opponentCount >= 8) break;  // Very few cars initially (reduced from 15 to 8)

        Line& line = lines[i];
        line.hasOpponent = true;
        line.opponentLane = dist_lane(rng);
        line.opponentOffset = dist_offset(rng);

        int carType = dist_car_type(rng);
        line.opCar.setTexture(opponentTextures[carType]);
        opponentCount++;
    }

    // Add scenery objects along the road - MORE FREQUENT AND RANDOM TREES + HOUSES ON RIGHT + GRASS ON LEFT
    if (hasSceneryTextures) {
        for (int i = 100; i < N; i += 20 + dist_spawn(rng) % 40) {  // MUCH more frequent: every 20-60 segments
            if (dist_spawn(rng) % 100 < 75) {  // 75% chance to place scenery
                Line& line = lines[i];
                line.hasScenery = true;

                // NEW WEIGHTED SELECTION with house placement logic
                int weightedChoice = dist_weighted_scenery(rng);
                if (weightedChoice < 4) {
                    line.sceneryType = 0; // Palm tree 1 (40% chance)
                    line.sceneryOnLeft = dist_side(rng) == 0; // Random side for palm trees
                }
                else if (weightedChoice < 7) {
                    line.sceneryType = 1; // Palm tree 2 (30% chance)
                    line.sceneryOnLeft = dist_side(rng) == 0; // Random side for palm trees
                }
                else if (weightedChoice < 8) {
                    line.sceneryType = 2; // House (10% chance)
                    line.sceneryOnLeft = false; // ALWAYS RIGHT SIDE for houses
                }
                else {
                    line.sceneryType = 3; // Grass (20% chance)
                    line.sceneryOnLeft = true; // ALWAYS LEFT SIDE for grass
                }

                line.scenerySprite.setTexture(sceneryTextures[line.sceneryType]);

                // Add random offset for more natural positioning
                line.opponentOffset = dist_offset(rng); // Reuse this for distance variety
            }
        }

        // ADDITIONAL PASS: Add even more palm trees in specific areas for lush roadside
        for (int i = 50; i < N; i += 35 + dist_spawn(rng) % 25) { // Another layer of trees
            if (dist_spawn(rng) % 100 < 40 && !lines[i].hasScenery) { // 40% chance, only if no scenery yet
                Line& line = lines[i];
                line.hasScenery = true;

                // Only palm trees in this pass for roadside density
                line.sceneryType = (dist_spawn(rng) % 2 == 0) ? 0 : 1; // 50/50 between palm types
                line.sceneryOnLeft = dist_side(rng) == 0; // Random side for palm trees
                line.scenerySprite.setTexture(sceneryTextures[line.sceneryType]);
                line.opponentOffset = dist_offset(rng);
            }
        }
    }

    // Game variables
    int pos = 0;
    int playerLane = 1;  // Middle lane (0=left, 1=middle, 2=right)
    float playerX = 0;
    float targetX = 0;
    int score = 0;
    int speed = 0;

    // Lane switching control
    bool leftPressed = false;
    bool rightPressed = false;

    // Boost system
    const int MAX_BOOSTS = 3;
    int boostsLeft = MAX_BOOSTS;
    int boostTimer = 0;
    bool isBoosting = false;

    Clock gameClock;
    float accumulator = 0;
    const float dt = 1.0f / 60.0f;

    bool isOver = false;

    // Main game loop
    while (window.isOpen()) {
        float frameTime = gameClock.restart().asSeconds();
        accumulator += frameTime;

        Event e;
        while (window.pollEvent(e)) {
            if (e.type == Event::Closed) {
                window.close();
            }

            if (isOver && e.type == Event::KeyPressed) {
                if (e.key.code == Keyboard::Y) {
                    // Reset game
                    isOver = false;
                    pos = 0;
                    playerLane = 1;  // Start in middle lane
                    playerX = 0;
                    targetX = 0;
                    score = 0;
                    boostsLeft = MAX_BOOSTS;
                    isBoosting = false;
                    boostTimer = 0;
                    leftPressed = false;
                    rightPressed = false;

                    // Reset opponents - start with fewer, add more over time
                    for (auto& line : lines) {
                        line.hasOpponent = false;
                        line.hasScenery = false;  // Reset scenery too
                    }
                    opponentCount = 0;
                    for (int i = 400; i < N; i += 150 + dist_spawn(rng) % 200) {
                        if (opponentCount >= 8) break;
                        Line& line = lines[i];
                        line.hasOpponent = true;
                        line.opponentLane = dist_lane(rng);
                        line.opponentOffset = dist_offset(rng);
                        int carType = dist_car_type(rng);
                        line.opCar.setTexture(opponentTextures[carType]);
                        opponentCount++;
                    }

                    // Re-add scenery with NEW placement logic
                    if (hasSceneryTextures) {
                        for (int i = 100; i < N; i += 20 + dist_spawn(rng) % 40) {
                            if (dist_spawn(rng) % 100 < 75) {
                                Line& line = lines[i];
                                line.hasScenery = true;

                                // NEW weighted selection with house/grass placement logic
                                int weightedChoice = dist_weighted_scenery(rng);
                                if (weightedChoice < 4) {
                                    line.sceneryType = 0; // Palm tree 1 (40% chance)
                                    line.sceneryOnLeft = dist_side(rng) == 0; // Random side
                                }
                                else if (weightedChoice < 7) {
                                    line.sceneryType = 1; // Palm tree 2 (30% chance)
                                    line.sceneryOnLeft = dist_side(rng) == 0; // Random side
                                }
                                else if (weightedChoice < 8) {
                                    line.sceneryType = 2; // House (10% chance)
                                    line.sceneryOnLeft = false; // ALWAYS RIGHT SIDE
                                }
                                else {
                                    line.sceneryType = 3; // Grass (20% chance)
                                    line.sceneryOnLeft = true; // ALWAYS LEFT SIDE
                                }

                                line.scenerySprite.setTexture(sceneryTextures[line.sceneryType]);
                                line.opponentOffset = dist_offset(rng);
                            }
                        }

                        // Additional pass for more palm trees
                        for (int i = 50; i < N; i += 35 + dist_spawn(rng) % 25) {
                            if (dist_spawn(rng) % 100 < 40 && !lines[i].hasScenery) {
                                Line& line = lines[i];
                                line.hasScenery = true;
                                line.sceneryType = (dist_spawn(rng) % 2 == 0) ? 0 : 1;
                                line.sceneryOnLeft = dist_side(rng) == 0; // Random side for palm trees
                                line.scenerySprite.setTexture(sceneryTextures[line.sceneryType]);
                                line.opponentOffset = dist_offset(rng);
                            }
                        }
                    }

                    if (soundEnabled) engine.play();
                    sfxOver.stop();
                }
                else if (e.key.code == Keyboard::N) {
                    window.close();
                }
            }

            if (!isOver && e.type == Event::KeyPressed) {
                if (e.key.code == Keyboard::Space && boostsLeft > 0 && !isBoosting) {
                    isBoosting = true;
                    boostTimer = 0;
                    boostsLeft--;
                    sfxBoost.play();
                }
            }
        }

        if (!isOver) {
            // Handle input for lane changes - instant switching on key press
            bool leftKeyPressed = Keyboard::isKeyPressed(Keyboard::Left) || Keyboard::isKeyPressed(Keyboard::A);
            bool rightKeyPressed = Keyboard::isKeyPressed(Keyboard::Right) || Keyboard::isKeyPressed(Keyboard::D);

            // Left lane change
            if (leftKeyPressed && !leftPressed && playerLane > 0) {
                playerLane--;
                leftPressed = true;
            }
            if (!leftKeyPressed) {
                leftPressed = false;
            }

            // Right lane change  
            if (rightKeyPressed && !rightPressed && playerLane < NUM_LANES - 1) {
                playerLane++;
                rightPressed = true;
            }
            if (!rightKeyPressed) {
                rightPressed = false;
            }

            // Calculate target position based on current lane
            // Lane 0 = -0.6, Lane 1 = 0, Lane 2 = 0.6
            targetX = (playerLane - 1) * 0.6f;

            // Smooth transition to target position
            playerX += (targetX - playerX) * 0.15f;

            // Update speed and position
            if (isBoosting) {
                speed = 400;
                boostTimer++;
                if (boostTimer > 120) {  // 2 seconds boost
                    isBoosting = false;
                    boostTimer = 0;
                }
            }
            else {
                speed = 200;
            }

            pos += speed;
            while (pos >= N * SEG_LEN) pos -= N * SEG_LEN;
            while (pos < 0) pos += N * SEG_LEN;

            score = pos / 100;

            // Dynamically spawn more opponents as game progresses
            if (score > 50 && score % 100 == 0) { // Every 100 points after score 50
                for (int i = (pos / SEG_LEN) + 500; i < (pos / SEG_LEN) + 700; i += 100 + dist_spawn(rng) % 150) {
                    if (i < N && !lines[i % N].hasOpponent && dist_spawn(rng) % 100 < 30) { // 30% chance
                        Line& line = lines[i % N];
                        line.hasOpponent = true;
                        line.opponentLane = dist_lane(rng);
                        line.opponentOffset = dist_offset(rng);
                        int carType = dist_car_type(rng);
                        line.opCar.setTexture(opponentTextures[carType]);
                    }
                }
            }

            // Clear window
            window.clear(Color(135, 206, 235));  // Sky blue

            // Draw panoramic background - CHANGED SECTION
            if (bgTex.getSize().x > 0) {
                // Calculate panoramic panning
                float maxPan = bgTex.getSize().x - WIDTH;
                float panX = (playerX * 0.5f + 0.5f) * maxPan;

                // Show more background - increased from half to 60%
                int skyHeight = HEIGHT * 0.6;
                background.setTextureRect(IntRect(int(panX), 0, WIDTH, skyHeight));
                window.draw(background);
            }

            // Draw road
            int startPos = pos / SEG_LEN;
            int camH = lines[startPos].y + 1500;
            int maxy = HEIGHT;
            float x = 0, dx = 0;

            // Draw road segments from far to near - MAXIMUM RANGE for ultra-distant scenery
            for (int n = startPos; n < startPos + 800; n++) { // INCREASED from 600 to 800 for ultra-distant visibility
                Line& l = lines[n % N];
                l.project(int(playerX * ROAD_W / 2 - x), camH, startPos * SEG_LEN - (n >= N ? N * SEG_LEN : 0));
                x += dx;
                dx += l.curve;

                l.clip = float(maxy);
                if (l.Y >= maxy) continue;
                maxy = int(l.Y);

                Line p = (n > 0) ? lines[(n - 1) % N] : l;

                // Only draw road quads for closer segments to maintain performance
                if (n < startPos + 300) {
                    // Alternate segment colors for road effect
                    bool isDark = ((n / 3) % 2) == 0;

                    // Less intense grass color (reduced green intensity)
                    Color grass = isDark ? Color(0, 120, 0) : Color(0, 135, 0); // Reduced from 154/170 to 120/135

                    drawQuad(window, grass, 0, int(p.Y), WIDTH, 0, int(l.Y), WIDTH);

                    // Draw road shoulder
                    Color rumble = isDark ? Color(170, 0, 0) : Color(255, 255, 255);
                    drawQuad(window, rumble, int(p.X), int(p.Y), int(p.W * 1.15f), int(l.X), int(l.Y), int(l.W * 1.15f)); // Reduced from 1.2f

                    // Draw road
                    Color road = isDark ? Color(70, 70, 70) : Color(80, 80, 80);
                    drawQuad(window, road, int(p.X), int(p.Y), int(p.W), int(l.X), int(l.Y), int(l.W));

                    // Draw shorter lane markings for corner strips
                    if (!isDark && p.W > 50) { // Only draw if road is wide enough
                        float laneW1 = p.W * 2.0f / NUM_LANES;
                        float laneW2 = l.W * 2.0f / NUM_LANES;
                        float laneX1 = p.X - p.W;
                        float laneX2 = l.X - l.W;

                        // Shorter lane markings (reduced width from 2 to 1)
                        int markingWidth = max(1, int(p.W * 0.005f)); // Adaptive width based on distance
                        for (int lane = 1; lane < NUM_LANES; lane++) {
                            drawQuad(window, Color::White,
                                int(laneX1 + laneW1 * lane), int(p.Y), markingWidth,
                                int(laneX2 + laneW2 * lane), int(l.Y), markingWidth);
                        }
                    }
                }
            }

            // Draw opponents and check collisions - MAXIMUM RANGE for ultra-distant scenery
            vector<FloatRect> opponentBounds;
            vector<int> opponentLanes;

            for (int n = startPos; n < startPos + 800; n++) { // INCREASED from 600 to 800 for ultra-distant scenery
                Line& l = lines[n % N];

                // Draw scenery first (behind cars) - allow ultra-distant scenery
                if (l.hasScenery && l.Y < HEIGHT + 200 && l.Y > -300) { // Ultra-generous Y bounds
                    l.drawScenery(window, pos);
                }

                // Only process opponents in closer range for performance
                if (n < startPos + 300 && l.hasOpponent && l.Y < HEIGHT && l.Y > -100) {
                    // Pass current player Z position for proper distance calculation
                    FloatRect oppBounds = l.drawOpponent(window, pos);

                    if (oppBounds.width > 0) {
                        opponentBounds.push_back(oppBounds);
                        opponentLanes.push_back(l.opponentLane);
                    }
                }
            }

            // Check collisions with all visible opponents
            float playerScreenX = WIDTH / 2 + playerX * WIDTH / 3;
            float playerScreenY = HEIGHT - 110;
            FloatRect playerRect(playerScreenX - 60, playerScreenY - 45, 120, 90);

            for (size_t i = 0; i < opponentBounds.size(); i++) {
                FloatRect& oppBounds = opponentBounds[i];
                int oppLane = opponentLanes[i];

                // Check if in same lane
                if (oppLane == playerLane) {
                    // Check if rectangles overlap
                    if (playerRect.intersects(oppBounds)) {
                        isOver = true;
                        if (soundEnabled) engine.stop();
                        sfxOver.play();
                        break;
                    }
                }
            }

            // Draw player car with better grounding
            playerScreenX = WIDTH / 2 + playerX * WIDTH / 3;
            player.setPosition(playerScreenX, HEIGHT - 110); // Adjusted to sit better on road

            // Reduced tilt effect for smoother animation
            float tilt = (targetX - playerX) * 15;
            player.setRotation(tilt);

            // Add more realistic shadow under player car
            RectangleShape shadow(Vector2f(110, 12));
            shadow.setFillColor(Color(0, 0, 0, 140));
            shadow.setPosition(playerScreenX - 55, HEIGHT - 65); // Shadow positioned on road surface
            window.draw(shadow);

            window.draw(player);

            // Draw UI
            stringstream ss;
            ss << "Score: " << score;
            tScore.setString(ss.str());
            window.draw(tScore);

            stringstream ss2;
            ss2 << "Speed: " << speed << " km/h";
            if (isBoosting) ss2 << " [BOOSTING!]";
            tSpeed.setString(ss2.str());
            window.draw(tSpeed);

            // Draw booster UI on right side
            if (hasBoosterUI) {
                const float marginRight = 10.f;
                const float marginTop = 4.f;
                const float vGap = 0.f;
                const int maxBoosters = 3;

                // Use only texture's actual pixel width (ignoring padding)
                float iconWidth = boosterIcon.getTextureRect().width * boosterIcon.getScale().x;

                // Booster text bounds
                sf::FloatRect textBounds = boosterText.getLocalBounds();
                boosterText.setOrigin(textBounds.left, textBounds.top);

                float totalIconRowWidth = maxBoosters * iconWidth;

                // Position text so icons fit under it, aligned to right
                float textX = WIDTH - marginRight - totalIconRowWidth / 2.f - textBounds.width / 2.f;
                float textY = marginTop;
                boosterText.setPosition(textX, textY);
                window.draw(boosterText);

                // Base position for first icon (centered under text)
                float baseX = boosterText.getPosition().x + textBounds.width / 2.f - totalIconRowWidth / 2.f;
                float baseY = boosterText.getPosition().y + textBounds.height + vGap;

                // Draw boosters touching (no transparent gap)
                for (int i = 0; i < maxBoosters; i++) {
                    if (i < boostsLeft) {
                        boosterIcon.setColor(sf::Color::White);
                    }
                    else {
                        boosterIcon.setColor(sf::Color(255, 255, 255, 80));
                    }
                    boosterIcon.setPosition(baseX + i * iconWidth, baseY);
                    window.draw(boosterIcon);
                }
            }






            window.display();
        }
        else {
            // Game over screen
            window.clear(Color(20, 20, 20));

            // Draw final score
            Text finalScore("Final Score: " + to_string(score), fontScore, 50);
            finalScore.setFillColor(Color::Yellow);
            finalScore.setPosition(WIDTH / 2 - finalScore.getGlobalBounds().width / 2, HEIGHT / 2 - 50);

            window.draw(tGameOver);
            window.draw(finalScore);
            window.draw(tPrompt);
            window.display();
        }
    }

    return 0;
}