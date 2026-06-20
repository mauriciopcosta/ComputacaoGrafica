/* Desafio M2 - Cubos Interativos
 *
 * Maurício Pereira da Costa - Computação Gráfica (Unisinos)
 *
 * Aqui eu peguei o Hello3D do M1 (que era só um triângulo amarelo) e fui
 * subindo pra 3D de verdade. O que eu quis aprender neste desafio:
 *  - desenhar um cubo de 36 vértices (6 faces, pintei cada face de uma cor
 *    diferente só pra eu enxergar pra que lado o cubo tá virado);
 *  - entrar com as matrizes model / view / projection (foi aqui que a coisa
 *    virou "3D" mesmo) e ligar o depth test;
 *  - girar o cubo em cada eixo de forma independente (X, Y, Z togglam);
 *  - andar com o cubo no teclado (WASD em X/Z, I/J em Y) e escalar com [ ];
 *  - ter VÁRIOS cubos na cena: N cria um novo, TAB escolhe qual recebe os
 *    comandos. Pra isso eu criei a struct Cube guardando o estado de cada um.
 *
 * Detalhe que me pegou: tudo que é movimento eu multiplico por dt (tempo entre
 * frames), senão num monitor de 144Hz o cubo dispara e em 60Hz ele se arrasta.
 */

#include <iostream>
#include <string>
#include <vector>
#include <assert.h>

using namespace std;

// GLAD
#include <glad/glad.h>

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Protótipo da função de callback de teclado
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);

// Protótipos das funções
int setupShader();
int setupGeometry();

// Dimensões da janela (pode ser alterado em tempo de execução)
const GLuint WIDTH = 1000, HEIGHT = 1000;

// Vertex Shader - rodado UMA vez por vértice na GPU.
// A linha que importa é a gl_Position: eu multiplico projection * view * model
// pela posição do vértice. É essa multiplicação de matrizes que leva o ponto
// do espaço do objeto -> mundo -> câmera -> tela. A cor eu só repasso adiante.
const GLchar* vertexShaderSource = "#version 450\n"
"layout (location = 0) in vec3 position;\n"
"layout (location = 1) in vec3 color;\n"
"uniform mat4 model;\n"
"uniform mat4 view;\n"
"uniform mat4 projection;\n"
"out vec4 finalColor;\n"
"void main()\n"
"{\n"
"gl_Position = projection * view * model * vec4(position, 1.0);\n"
"finalColor = vec4(color, 1.0);\n"
"}\0";

// Fragment Shader - roda por pixel. Neste M2 ainda não tem iluminação nenhuma,
// então ele só joga na tela a cor que veio interpolada do vertex shader.
const GLchar* fragmentShaderSource = "#version 450\n"
"in vec4 finalColor;\n"
"out vec4 color;\n"
"void main()\n"
"{\n"
"color = finalColor;\n"
"}\n\0";

// Cada cubo da cena é um Cube. Guardo o estado de cada um aqui dentro pra que
// eles sejam independentes: movo/giro um sem mexer nos outros.
struct Cube {
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
	float scale = 1.0f;
	// Qual eixo tá girando agora: 0 = parado, 1 = X, 2 = Y, 3 = Z
	int rotAxis = 0;
	// Guardo o ângulo acumulado em vez de zerar: assim, quando eu paro a
	// rotação, o cubo "congela" na pose em que estava (não dá um salto).
	float angle = 0.0f;
};

vector<Cube> cubes;
int selected = 0;

// Velocidades das transformações por segundo
const float MOVE_SPEED = 1.5f;
const float SCALE_SPEED = 1.5f;
const float ROT_SPEED = 1.5f;

int main()
{
	// Inicialização da GLFW
	glfwInit();

	// Criação da janela GLFW
	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Desafio M2 - Cubos Interativos -- Mauricio Pereira da Costa", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	glfwSetKeyCallback(window, key_callback);

	// GLAD: carrega todos os ponteiros das funções da OpenGL
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
	}

	const GLubyte* renderer = glGetString(GL_RENDERER);
	const GLubyte* version = glGetString(GL_VERSION);
	cout << "Renderer: " << renderer << endl;
	cout << "OpenGL version supported " << version << endl;

	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);

	GLuint shaderID = setupShader();
	GLuint VAO = setupGeometry();

	glUseProgram(shaderID);

	// Pego o "endereço" de cada uniform dentro do shader uma vez só, pra depois
	// só mandar os dados. É a ponte entre o meu C++ e as variáveis do shader.
	GLint modelLoc = glGetUniformLocation(shaderID, "model");
	GLint viewLoc = glGetUniformLocation(shaderID, "view");
	GLint projLoc = glGetUniformLocation(shaderID, "projection");

	// View = onde está a câmera. Aqui ela é FIXA: lookAt coloca o observador em
	// (0,0,3) olhando pra origem, com o "cima" sendo +Y. (No M5 isso vira uma
	// classe Camera que anda; aqui é só uma matriz montada uma vez.)
	glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 3.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);
	// glUniformMatrix4fv = "manda essa matriz pro shader". É a passagem de uniform.
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

	// Projection perspectiva: FOV de 45°, a razão de aspecto da janela e os
	// planos near/far (0.1 e 100). É ela que dá a sensação de profundidade
	// (coisa longe parece menor). View e projection são fixas, então mando
	// uma vez aqui fora do loop; só a model muda por cubo.
	glm::mat4 projection = glm::perspective(
		glm::radians(45.0f),
		(float)width / (float)height,
		0.1f, 100.0f
	);
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

	// Depth test ligado: sem isso a face de trás às vezes aparece na frente,
	// porque o OpenGL desenha na ordem dos vértices e não pela profundidade.
	glEnable(GL_DEPTH_TEST);

	// Começo a cena com um cubo só.
	cubes.push_back(Cube());

	float lastTime = (float)glfwGetTime();

	// Loop principal: roda a cada frame até a janela fechar.
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		// dt = tempo que passou desde o frame anterior. Multiplico todo
		// movimento por ele pra ficar igual em qualquer taxa de quadros.
		float now = (float)glfwGetTime();
		float dt = now - lastTime;
		lastTime = now;

		// Aqui leio as teclas que valem "enquanto seguro" (andar e escalar).
		// As ações de toque único (girar, criar cubo, TAB) ficam na callback.
		if (!cubes.empty())
		{
			Cube& c = cubes[selected];

			// Translação - eixos X e Z (WASD)
			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) c.position.x -= MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) c.position.x += MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) c.position.z -= MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) c.position.z += MOVE_SPEED * dt;

			// Translação - eixo Y (I sobe, J desce)
			if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) c.position.y += MOVE_SPEED * dt;
			if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) c.position.y -= MOVE_SPEED * dt;

			// Escala uniforme - [ diminui, ] aumenta
			if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
				c.scale -= SCALE_SPEED * dt * c.scale;
			if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
				c.scale += SCALE_SPEED * dt * c.scale;
			if (c.scale < 0.05f) c.scale = 0.05f;
		}

		// Limpa os buffers de cor e profundidade
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindVertexArray(VAO);

		// Desenho cada cubo. Como todos compartilham a mesma geometria (mesmo
		// VAO), o que muda de um pro outro é só a matriz model que eu monto aqui.
		for (size_t i = 0; i < cubes.size(); i++)
		{
			Cube& c = cubes[i];

			// Só somo ângulo se o cubo estiver girando em algum eixo.
			if (c.rotAxis != 0) c.angle += ROT_SPEED * dt;

			// Monto a model do zero (identidade) e aplico, NESTA ordem:
			// translada -> gira -> escala. Em GLM a última chamada é a que
			// "encosta" primeiro no vértice, então na prática ele é escalado,
			// depois girado e por fim levado pra posição.
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, c.position);

			if (c.rotAxis == 1)
				model = glm::rotate(model, c.angle, glm::vec3(1.0f, 0.0f, 0.0f));
			else if (c.rotAxis == 2)
				model = glm::rotate(model, c.angle, glm::vec3(0.0f, 1.0f, 0.0f));
			else if (c.rotAxis == 3)
				model = glm::rotate(model, c.angle, glm::vec3(0.0f, 0.0f, 1.0f));

			model = glm::scale(model, glm::vec3(c.scale));

			// Mando a model deste cubo pro shader e mando desenhar os 36 vértices.
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

			glDrawArrays(GL_TRIANGLES, 0, 36);
		}

		glBindVertexArray(0);
		glfwSwapBuffers(window);
	}

	glDeleteVertexArrays(1, &VAO);
	glfwTerminate();
	return 0;
}

// Callback de teclado: aqui só trato o que é "apertou uma vez" (GLFW_PRESS),
// tipo togglar rotação, criar cubo e trocar de seleção. Andar/escalar não fica
// aqui porque eu quero que aconteça enquanto a tecla está segurada (isso eu leio
// lá no loop com glfwGetKey).
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (action != GLFW_PRESS) return;

	if (key == GLFW_KEY_ESCAPE)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (cubes.empty()) return;
	Cube& c = cubes[selected];

	// X/Y/Z funcionam como liga/desliga: se já tá girando naquele eixo, apertar
	// de novo zera o rotAxis (para). Como eu não zero o ângulo, ele congela.
	if (key == GLFW_KEY_X) c.rotAxis = (c.rotAxis == 1) ? 0 : 1;
	if (key == GLFW_KEY_Y) c.rotAxis = (c.rotAxis == 2) ? 0 : 2;
	if (key == GLFW_KEY_Z) c.rotAxis = (c.rotAxis == 3) ? 0 : 3;

	// N - cria um cubo novo já deslocado pro lado pra não nascer em cima do
	// anterior, e deixa ele como o selecionado.
	if (key == GLFW_KEY_N)
	{
		Cube novo;
		novo.position = glm::vec3(
			-1.0f + 0.4f * (float)cubes.size(),
			0.0f,
			0.0f
		);
		novo.scale = 0.5f;
		cubes.push_back(novo);
		selected = (int)cubes.size() - 1;
		cout << "Novo cubo instanciado. Total: " << cubes.size()
			 << " | Selecionado: " << selected << endl;
	}

	// TAB - passa a seleção pro próximo cubo (uso resto da divisão pra
	// dar a volta e voltar pro 0 depois do último).
	if (key == GLFW_KEY_TAB)
	{
		selected = (selected + 1) % (int)cubes.size();
		cout << "Cubo selecionado: " << selected << endl;
	}
}

// Compila o vertex e o fragment, linka os dois num "programa" e devolve o id.
// Em cada etapa eu checo o status e imprimo o log se der erro de compilação -
// isso me salvou várias vezes quando errava a sintaxe do GLSL.
int setupShader()
{
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);
	GLint success;
	GLchar infoLog[512];
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
	}
	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
	}
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return shaderProgram;
}

// Aqui eu monto a geometria do cubo na "mão": 6 faces x 2 triângulos x 3
// vértices = 36 vértices, cada um com posição + cor. Como cubo não tem como ser
// fechado com triângulos compartilhando vértice de boa, deixei tudo solto mesmo.
//
// O VBO é o array de números que mando pra GPU; o VAO é a "receita" que diz pra
// ela como ler esse array. Cada vértice tem 6 floats em sequência:
//   atributo 0 (location 0) -> posição (x, y, z)  -> 3 floats, começa no offset 0
//   atributo 1 (location 1) -> cor     (r, g, b)  -> 3 floats, começa no offset 3
//   stride = 6 floats (o tamanho de um vértice inteiro, pra GPU saber pular)
// Esses location 0 e 1 batem com os "layout (location = ...)" lá no shader.
int setupGeometry()
{
	GLfloat vertices[] = {
		// Face frontal (+Z) - vermelho
		-0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		-0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,

		// Face traseira (-Z) - verde
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,

		// Face esquerda (-X) - azul
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,

		// Face direita (+X) - amarelo
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.0f,

		// Face superior (+Y) - magenta
		-0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 1.0f,

		// Face inferior (-Y) - ciano
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
	};

	GLuint VBO, VAO;

	// Crio o VBO e copio meu array de vértices pra GPU (STATIC_DRAW = não vai
	// mudar mais, então a placa pode otimizar).
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	// Ensino a GPU a achar a POSIÇÃO: 3 floats, pulando de 6 em 6, começando no 0.
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);

	// E a achar a COR: 3 floats, mesmo passo, mas começando depois dos 3 da posição.
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return VAO;
}
