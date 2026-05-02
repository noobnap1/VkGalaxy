#include "game.hpp"

//----------------------------------------------------------------------------//
#define CAMERA_FOV 70.0f
#define CAMERA_MAX_DIST 8000.0f
#define CAMERA_MIN_TILT -89.0f 
#define CAMERA_MAX_TILT 89.0f
#define CAMERA_MAX_POSITION 700000.0f

//----------------------------------------------------------------------------//
bool _game_camera_init(GameCamera* cam);
void _game_camera_update(GameCamera* cam, f32 dt, GLFWwindow* window);
void _game_camera_cursor_moved(GameCamera* cam, f32 dx, f32 dy);
void _game_camera_scroll(GameCamera* cam, f32 amt);

//----------------------------------------------------------------------------//
void _game_cursor_pos_callback(GLFWwindow* window, f64 x, f64 y);
void _game_key_callback(GLFWwindow* window, int32 key, int32 scancode, int32 action, int32 mods);
void _game_scroll_callback(GLFWwindow* window, f64 x, f64 y);

//----------------------------------------------------------------------------//
template<typename T>
void _game_decay_to(T& value, T target, f32 rate, f32 dt);

//----------------------------------------------------------------------------//
static void _game_message_log(const char* message, const char* file, int32 line);
#define MSG_LOG(m) _game_message_log(m, __FILENAME__, __LINE__)

static void _game_error_log(const char* message, const char* file, int32 line);
#define ERROR_LOG(m) _game_error_log(m, __FILENAME__, __LINE__)

//----------------------------------------------------------------------------//

// World-space positions of each galaxy. Add or remove entries freely.
// Spacing of ~10000 puts galaxies clearly apart given a maxRad of 3500.
static const qm::vec3 GALAXY_OFFSETS[] =
{
	{ -5000.0f, 0.0f, 0.0f },
	{5000.0f, 1000.0f, 0.0f},
};
static const uint32 GALAXY_COUNT = sizeof(GALAXY_OFFSETS) / sizeof(GALAXY_OFFSETS[0]);

//----------------------------------------------------------------------------//
bool game_init(GameState** state)
{
	*state = (GameState*)malloc(sizeof(GameState));
	GameState* s = *state;

	if (!s)
	{
		ERROR_LOG("failed to allocate GameState struct");
		return false;
	}

	if (!draw_init(&s->drawState))
	{
		ERROR_LOG("failed to intialize rendering");
		return false;
	}

	if (!_game_camera_init(&s->cam))
	{
		ERROR_LOG("failed to initialize camera");
		return false;
	}

	glfwSetWindowUserPointer(s->drawState->instance->window, s);
	glfwSetCursorPosCallback(s->drawState->instance->window, _game_cursor_pos_callback);
	glfwSetKeyCallback(s->drawState->instance->window, _game_key_callback);
	glfwSetScrollCallback(s->drawState->instance->window, _game_scroll_callback);

	return true;
}

void game_quit(GameState* s)
{
	draw_quit(s->drawState);
	free(s);
}

//----------------------------------------------------------------------------//
void game_main_loop(GameState* s)
{
	f32 lastTime = (f32)glfwGetTime();

	while (!glfwWindowShouldClose(s->drawState->instance->window))
	{
		f32 curTime = (f32)glfwGetTime();
		f32 dt = curTime - lastTime;
		lastTime = curTime;

		_game_camera_update(&s->cam, dt, s->drawState->instance->window);

		// Build base draw params from the camera
		DrawParams drawParams;
		drawParams.cam.pos = s->cam.pos;
		drawParams.cam.up = s->cam.up;
		drawParams.cam.target = s->cam.center;
		drawParams.cam.dist = s->cam.dist;
		drawParams.cam.fov = CAMERA_FOV;

		// Begin the frame — acquires swapchain image, uploads camera, opens command buffer
		uint32 frameIdx, imageIdx;
		if (draw_begin_frame(s->drawState, &drawParams, &frameIdx, &imageIdx))
		{
			for (uint32 i = 0; i < GALAXY_COUNT; i++)
				draw_render_galaxy(s->drawState, &drawParams, frameIdx, imageIdx, GALAXY_OFFSETS[i]);

			// End the frame — closes command buffer, submits, presents
			draw_end_frame(s->drawState, frameIdx, imageIdx);
		}

		glfwPollEvents();
	}
}

//----------------------------------------------------------------------------//
bool _game_camera_init(GameCamera* cam)
{
	if (!cam)
	{
		ERROR_LOG("failed to allocate GameCamera struct");
		return false;
	}

	cam->up = { 0.0f, 1.0f, 0.0f };

	cam->pos = { 0.0f, 1500.0f, 5000.0f };
	cam->center = { 0.0f, 0.0f,    0.0f };

	cam->dist = cam->targetDist = 1.0f;
	cam->angle = cam->targetAngle = 0.0f;
	cam->tilt = cam->targetTilt = 0.0f;

	return true;
}

void _game_camera_update(GameCamera* cam, f32 dt, GLFWwindow* window)
{
	f32 camSpeed = 5000.0f * dt;
	f32 angleSpeed = 90.0f * dt;
	f32 tiltSpeed = 90.0f * dt;

	// Rotations (arrow keys)
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) cam->angle -= angleSpeed;
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) cam->angle += angleSpeed;

	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		cam->tilt = fmaxf(cam->tilt - tiltSpeed, CAMERA_MIN_TILT);
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		cam->tilt = fminf(cam->tilt + tiltSpeed, CAMERA_MAX_TILT);

	// True forward direction from yaw + pitch
	f32 yawRad = cam->angle * (3.14159f / 180.0f);
	f32 pitchRad = cam->tilt * (3.14159f / 180.0f);

	qm::vec3 forward;
	forward.x = sinf(yawRad) * cosf(pitchRad);
	forward.y = -sinf(pitchRad);
	forward.z = -cosf(yawRad) * cosf(pitchRad);
	forward = qm::normalize(forward);

	qm::vec3 worldUp = { 0.0f, 1.0f, 0.0f };
	qm::vec3 right = qm::normalize(qm::cross(forward, worldUp));
	qm::vec3 camUp = qm::normalize(qm::cross(right, forward));

	// Movement (WASD + EQ)
	qm::vec3 movement(0.0f, 0.0f, 0.0f);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) movement = movement + forward;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) movement = movement - forward;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) movement = movement + right;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) movement = movement - right;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) movement = movement + camUp;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) movement = movement - camUp;

	if (qm::length(movement) > 0.0f)
		cam->pos = cam->pos + (qm::normalize(movement) * camSpeed);

	// World bounds clamp
	if (qm::length(cam->pos) > CAMERA_MAX_POSITION)
		cam->pos = qm::normalize(cam->pos) * CAMERA_MAX_POSITION;

	// Feed look-at target to renderer
	cam->center = cam->pos + forward;

	// Sync targets so inputs don't interpolate from stale data
	cam->targetAngle = cam->angle;
	cam->targetTilt = cam->tilt;
	cam->targetCenter = cam->pos;
}

void _game_camera_cursor_moved(GameCamera* cam, f32 dx, f32 dy)
{
	f32 mouseSensitivity = 0.2f;

	cam->angle += dx * mouseSensitivity;
	cam->tilt += dy * mouseSensitivity;

	cam->tilt = fmaxf(CAMERA_MIN_TILT, fminf(cam->tilt, CAMERA_MAX_TILT));
}

void _game_camera_scroll(GameCamera* cam, f32 amt) {}

//----------------------------------------------------------------------------//
void _game_cursor_pos_callback(GLFWwindow* window, f64 x, f64 y)
{
	GameState* s = (GameState*)glfwGetWindowUserPointer(window);

	static f64  lastX = x;
	static f64  lastY = y;
	static bool firstMouse = true;

	if (firstMouse)
	{
		lastX = x;
		lastY = y;
		firstMouse = false;
	}

	f32 dx = (f32)(x - lastX);
	f32 dy = (f32)(y - lastY);
	lastX = x;
	lastY = y;

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
	{
		_game_camera_cursor_moved(&s->cam, dx, dy);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

void _game_key_callback(GLFWwindow* window, int32 key, int32 scancode, int32 action, int32 mods) {}

void _game_scroll_callback(GLFWwindow* window, f64 x, f64 y) {}

//----------------------------------------------------------------------------//
template<typename T>
void _game_decay_to(T& value, T target, f32 rate, f32 dt)
{
	value += (target - value) * (1.0f - powf(rate, 1000.0f * dt));
}

//----------------------------------------------------------------------------//
static void _game_message_log(const char* message, const char* file, int32 line)
{
	printf("GAME MESSAGE in %s at line %i - \"%s\"\n\n", file, line, message);
}

static void _game_error_log(const char* message, const char* file, int32 line)
{
	printf("GAME ERROR in %s at line %i - \"%s\"\n\n", file, line, message);
}