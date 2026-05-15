/* Atividade Vivencial - Modulo 2
 *
 * Extensao do desafio M2: carrega todos os arquivos .obj presentes em
 * assets/Modelos3D/ e permite selecionar um objeto e aplicar transformacoes
 * (translacao, rotacao e escala) sobre ele via teclado.
 *
 * Autor: Mauricio Pereira da Costa - Computacao Grafica (Unisinos)
 *
 * Controles:
 *   TAB         : cicla a selecao para o proximo objeto
 *   T           : entra em modo TRANSLACAO  (setas X/Y, PageUp/PageDown -> Z)
 *   R           : entra em modo ROTACAO    (X/Y/Z togglam rotacao no eixo)
 *   S           : entra em modo ESCALA     (X/Y/Z aumentam, Shift inverte;
 *                                           [ ] aplicam escala uniforme)
 *   ESC         : sai
 */

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

using namespace std;
namespace fs = std::filesystem;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- Prototipos ---
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
GLuint setupShader();
GLuint loadSimpleOBJ(const string& filePATH, int& nVertices, glm::vec3 color);

// --- Janela ---
const GLuint WIDTH = 1000, HEIGHT = 1000;

// --- Shaders ---
const GLchar* vertexShaderSource = R"(
#version 450
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec4 vColor;
void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
    vColor = vec4(color, 1.0);
}
)";

const GLchar* fragmentShaderSource = R"(
#version 450
in vec4 vColor;
uniform float tint;
uniform int  wireframe; // 0 = cor normal, 1 = branco solido (linhas)
out vec4 color;
void main() {
    if (wireframe == 1) color = vec4(1.0, 1.0, 1.0, 1.0);
    else                color = clamp(vColor * tint, 0.0, 1.0);
}
)";

// --- Modelo ---
struct OBJ {
    string name;
    GLuint VAO = 0;
    int nVertices = 0;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);
    glm::vec3 angles   = glm::vec3(0.0f); // angulo acumulado por eixo
    glm::ivec3 rotOn   = glm::ivec3(0);   // 1 = girando naquele eixo
};

vector<OBJ> objects;
int selected = 0;

enum class Mode { Translate, Rotate, Scale };
Mode currentMode = Mode::Translate;

bool showWireframe = true; // L alterna globalmente

const float MOVE_SPEED  = 1.5f;
const float ROT_SPEED   = 1.5f;
const float SCALE_SPEED = 1.5f;

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::Translate: return "TRANSLACAO";
        case Mode::Rotate:    return "ROTACAO";
        case Mode::Scale:     return "ESCALA";
    }
    return "?";
}

int main() {
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(
        WIDTH, HEIGHT,
        "Atividade Vivencial M2 - Multi OBJ - Mauricio Pereira da Costa",
        nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cout << "Failed to initialize GLAD" << endl;
        return -1;
    }

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    GLint modelLoc     = glGetUniformLocation(shaderID, "model");
    GLint viewLoc      = glGetUniformLocation(shaderID, "view");
    GLint projLoc      = glGetUniformLocation(shaderID, "projection");
    GLint tintLoc      = glGetUniformLocation(shaderID, "tint");
    GLint wireframeLoc = glGetUniformLocation(shaderID, "wireframe");

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 6.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glEnable(GL_DEPTH_TEST);

    // --- Carrega todos os .obj de assets/Modelos3D ---
    const string modelsDir = "../assets/Modelos3D";
    vector<glm::vec3> palette = {
        {1.0f, 0.35f, 0.35f},
        {0.35f, 0.85f, 0.45f},
        {0.4f,  0.55f, 1.0f},
        {1.0f,  0.85f, 0.3f},
        {0.85f, 0.4f,  1.0f},
        {0.3f,  0.95f, 0.95f}
    };

    if (!fs::exists(modelsDir)) {
        cerr << "Pasta nao encontrada: " << modelsDir
             << "\n(execute o binario a partir da pasta build/)" << endl;
        return -1;
    }

    vector<fs::path> objPaths;
    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".obj")
            objPaths.push_back(entry.path());
    }
    sort(objPaths.begin(), objPaths.end());

    if (objPaths.empty()) {
        cerr << "Nenhum .obj em " << modelsDir << endl;
        return -1;
    }

    int idx = 0;
    float spacing = 2.2f;
    float startX  = -spacing * (objPaths.size() - 1) * 0.5f;
    for (const auto& p : objPaths) {
        OBJ o;
        o.name = p.filename().string();
        o.VAO  = loadSimpleOBJ(p.string(), o.nVertices, palette[idx % palette.size()]);
        if (o.VAO == 0) { idx++; continue; }
        o.position = glm::vec3(startX + spacing * idx, 0.0f, 0.0f);
        o.scale    = glm::vec3(0.6f);
        objects.push_back(o);
        cout << "[" << objects.size() - 1 << "] " << o.name
             << " (" << o.nVertices << " vertices)" << endl;
        idx++;
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado com sucesso." << endl;
        return -1;
    }

    cout << "\nControles: TAB cicla | T translacao | R rotacao | S escala | ESC sai\n";
    cout << "Modo inicial: " << modeName(currentMode) << endl;
    cout << "Selecionado: [" << selected << "] " << objects[selected].name << endl;

    float lastTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;

        OBJ& sel = objects[selected];

        // --- Entrada continua dependente do modo ---
        if (currentMode == Mode::Translate) {
            if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) sel.position.x -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) sel.position.x += MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) sel.position.y += MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) sel.position.y -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS) sel.position.z -= MOVE_SPEED * dt;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) sel.position.z += MOVE_SPEED * dt;
        }
        else if (currentMode == Mode::Scale) {
            bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS
                      || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            float dir = shift ? -1.0f : 1.0f;
            float factor = 1.0f + dir * SCALE_SPEED * dt;

            if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) sel.scale.x *= factor;
            if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) sel.scale.y *= factor;
            if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) sel.scale.z *= factor;

            // Escala uniforme — aceita [ ] (US) e - = (alternativa universal p/ ABNT2)
            if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_MINUS)         == GLFW_PRESS)
                sel.scale *= (1.0f - SCALE_SPEED * dt);
            if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_EQUAL)         == GLFW_PRESS)
                sel.scale *= (1.0f + SCALE_SPEED * dt);

            sel.scale = glm::max(sel.scale, glm::vec3(0.05f));
        }

        // Rotacao acumula para TODOS os objetos com rotOn ligado,
        // nao so o selecionado — assim varios podem girar ao mesmo tempo.
        for (auto& o : objects) {
            if (o.rotOn.x) o.angles.x += ROT_SPEED * dt;
            if (o.rotOn.y) o.angles.y += ROT_SPEED * dt;
            if (o.rotOn.z) o.angles.z += ROT_SPEED * dt;
        }

        // --- Render ---
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (size_t i = 0; i < objects.size(); ++i) {
            OBJ& o = objects[i];
            glm::mat4 model(1.0f);
            model = glm::translate(model, o.position);
            model = glm::rotate(model, o.angles.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, o.angles.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, o.angles.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, o.scale);

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glBindVertexArray(o.VAO);

            // Passada 1: preenchido com a cor do objeto.
            // Polygon offset so eh necessario quando vamos sobrepor wireframe.
            if (showWireframe) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 1.0f);
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUniform1i(wireframeLoc, 0);
            glUniform1f(tintLoc, ((int)i == selected) ? 1.6f : 0.85f);
            glDrawArrays(GL_TRIANGLES, 0, o.nVertices);
            if (showWireframe) glDisable(GL_POLYGON_OFFSET_FILL);

            // Passada 2: arestas brancas sobre o preenchido (so se ativo).
            if (showWireframe) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(1.0f);
                glUniform1i(wireframeLoc, 1);
                glDrawArrays(GL_TRIANGLES, 0, o.nVertices);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        glBindVertexArray(0);

        glfwSwapBuffers(window);
    }

    for (auto& o : objects) glDeleteVertexArrays(1, &o.VAO);
    glfwTerminate();
    return 0;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }
    if (objects.empty()) return;

    if (key == GLFW_KEY_TAB) {
        selected = (selected + 1) % (int)objects.size();
        cout << "Selecionado: [" << selected << "] " << objects[selected].name << endl;
        return;
    }

    if (key == GLFW_KEY_T) { currentMode = Mode::Translate; cout << "Modo: " << modeName(currentMode) << endl; return; }
    if (key == GLFW_KEY_R) { currentMode = Mode::Rotate;    cout << "Modo: " << modeName(currentMode) << endl; return; }
    if (key == GLFW_KEY_S) { currentMode = Mode::Scale;     cout << "Modo: " << modeName(currentMode) << endl; return; }

    if (key == GLFW_KEY_L) {
        showWireframe = !showWireframe;
        cout << "Wireframe: " << (showWireframe ? "ON" : "OFF") << endl;
        return;
    }

    // Em modo rotacao, X/Y/Z togglam giro no eixo
    if (currentMode == Mode::Rotate) {
        OBJ& o = objects[selected];
        if (key == GLFW_KEY_X) o.rotOn.x = 1 - o.rotOn.x;
        if (key == GLFW_KEY_Y) o.rotOn.y = 1 - o.rotOn.y;
        if (key == GLFW_KEY_Z) o.rotOn.z = 1 - o.rotOn.z;
    }
}

GLuint setupShader() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    GLint ok; GLchar log[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); cout << "VS:\n" << log << endl; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); cout << "FS:\n" << log << endl; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); cout << "LINK:\n" << log << endl; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// Adaptado do snippet LoadSimpleOBJ da professora Rossana Baptista Queiroz.
// Diferenca: a cor por vertice e parametrizada (em vez de hardcoded).
GLuint loadSimpleOBJ(const string& filePATH, int& nVertices, glm::vec3 color) {
    vector<glm::vec3> vertices;
    vector<glm::vec2> texCoords;
    vector<glm::vec3> normals;
    vector<GLfloat>   vBuffer;

    ifstream arq(filePATH.c_str());
    if (!arq.is_open()) {
        cerr << "Erro ao abrir: " << filePATH << endl;
        nVertices = 0;
        return 0;
    }

    string line;
    while (getline(arq, line)) {
        istringstream ss(line);
        string word;
        ss >> word;

        if (word == "v") {
            glm::vec3 v; ss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        } else if (word == "vt") {
            glm::vec2 vt; ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        } else if (word == "vn") {
            glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        } else if (word == "f") {
            while (ss >> word) {
                int vi = 0, ti = 0, ni = 0;
                istringstream tok(word);
                string idx;
                if (getline(tok, idx, '/')) vi = !idx.empty() ? stoi(idx) - 1 : 0;
                if (getline(tok, idx, '/')) ti = !idx.empty() ? stoi(idx) - 1 : 0;
                if (getline(tok, idx))      ni = !idx.empty() ? stoi(idx) - 1 : 0;

                if (vi < 0 || vi >= (int)vertices.size()) continue;
                vBuffer.push_back(vertices[vi].x);
                vBuffer.push_back(vertices[vi].y);
                vBuffer.push_back(vertices[vi].z);
                vBuffer.push_back(color.r);
                vBuffer.push_back(color.g);
                vBuffer.push_back(color.b);
            }
        }
    }
    arq.close();

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = (int)(vBuffer.size() / 6);
    return VAO;
}
