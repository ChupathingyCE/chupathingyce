/*
SC_ERRORS.C

The player's words for what went wrong (steam_content.h): Steam's EResult
at each step of a download, mapped to "Not owned", "Sign in", "Offline" or
"Failed". The reason itself goes to the log.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_internal.h"

const char *steam_content_error_text(int error)
{
	switch (error)
	{
	case STEAM_CONTENT_OK: return "";
	case STEAM_CONTENT_NOT_OWNED: return "Not owned";
	case STEAM_CONTENT_SIGN_IN: return "Sign in";
	case STEAM_CONTENT_OFFLINE: return "Offline";
	case STEAM_CONTENT_CANCELLED: return "Cancelled";
	case STEAM_CONTENT_TOO_LARGE: return "Too large";
	case STEAM_CONTENT_DISK_FULL: return "Disk full";
	default: return "Failed";
	}
}

int steam_content_error_from_result(int step, int eresult)
{
	if (eresult == SC_RESULT_OK)
		return STEAM_CONTENT_OK;
	/* (no answer at all, or Steam saying it can't be reached right now) */
	if (eresult == 0 || eresult == SC_RESULT_NO_CONNECTION || eresult == SC_RESULT_TIMEOUT ||
		eresult == SC_RESULT_SERVICE_UNAVAILABLE || eresult == SC_RESULT_TRY_ANOTHER_CM ||
		eresult == SC_RESULT_CONNECT_FAILED || eresult == SC_RESULT_IO_FAILURE ||
		eresult == SC_RESULT_REMOTE_DISCONNECT)
	{
		return STEAM_CONTENT_OFFLINE;
	}
	switch (step)
	{
	case STEAM_CONTENT_STEP_CONNECT:
		return STEAM_CONTENT_OFFLINE;
	case STEAM_CONTENT_STEP_LOGON:
		/* (the stored sign-in refused: expired, revoked, the password changed) */
		if (eresult == SC_RESULT_INVALID_PASSWORD || eresult == SC_RESULT_ACCESS_DENIED ||
			eresult == SC_RESULT_EXPIRED || eresult == SC_RESULT_REVOKED || eresult == SC_RESULT_ACCOUNT_NOT_FOUND ||
			eresult == SC_RESULT_ACCOUNT_LOGON_DENIED || eresult == SC_RESULT_INVALID_LOGIN_AUTH_CODE ||
			eresult == SC_RESULT_NOT_LOGGED_ON || eresult == SC_RESULT_SESSION_REPLACED ||
			eresult == SC_RESULT_LOGGED_IN_ELSEWHERE)
		{
			return STEAM_CONTENT_SIGN_IN;
		}
		return STEAM_CONTENT_FAILED;
	case STEAM_CONTENT_STEP_DEPOT_KEY:
	case STEAM_CONTENT_STEP_MANIFEST:
		if (eresult == SC_RESULT_ACCESS_DENIED || eresult == SC_RESULT_FAIL)
			return STEAM_CONTENT_NOT_OWNED;
		if (eresult == SC_RESULT_NOT_LOGGED_ON)
			return STEAM_CONTENT_SIGN_IN;
		return STEAM_CONTENT_FAILED;
	case STEAM_CONTENT_STEP_CHUNK:
		/* (an HTTP status from a content server) */
		if (eresult == 401 || eresult == 403)
			return STEAM_CONTENT_NOT_OWNED;
		return STEAM_CONTENT_FAILED;
	default:
		return STEAM_CONTENT_FAILED;
	}
}
#endif /* HALO_MCC */
