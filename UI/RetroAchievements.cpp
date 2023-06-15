#include "ext/rcheevos/include/rcheevos.h"
#include "ext/rcheevos/include/rc_api_user.h"

#include "UI/RetroAchievements.h"

RetroAchievements g_retroAchievements;

RetroAchievements::RetroAchievements() {
	runtime_ = rc_runtime_alloc();
}

RetroAchievements::~RetroAchievements() {
	if (runtime_) {
		rc_runtime_destroy(runtime_);
	}
}

void RetroAchievements::Login(const char *username, const char *password) {
	rc_api_login_request_t api_params{};

	api_params.username = username;
	api_params.api_token = password;

	rc_api_request_t api_request;
	rc_api_init_login_request(&api_request, &api_params);

	/*
	rc_api_login_response_t api_response;
	int result = rc_api_process_login_response(&api_response, response_body);
	if (result != RC_OK)
	{
		handle_error(status_code, rc_error_str(result));
	} else if (!api_response.response.succeeded)
	{
		handle_error(status_code, api_response.response.error_message);
	} else
	{
		handle_success(&api_response);
	}

	rc_api_destroy_login_response(&api_response);
	free(response_body);
	*/
}

void RetroAchievements::Logout() {

}
