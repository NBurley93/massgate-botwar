# The botwar hook: dbghelp.dll for the World in Conflict game folder (LGPL-3.0, see LICENSE).
# It is built into bin/<config>/hook/ so that Massgate, which imports dbghelp.dll, never loads it.

# Link the CRT statically so the DLL can be dropped into the game folder on its own.
foreach(config DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
	string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach()

add_library(BotwarHook SHARED
	hook.h
	hook.cpp
	dbghelp_forward.cpp
	dbghelp_exports.inc
	wic_ds.cpp
	bot_names.cpp
)

set_target_properties(BotwarHook
	PROPERTIES
		OUTPUT_NAME dbghelp
		CXX_STANDARD 17
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/hook"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/hook"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/MinSizeRel/hook"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/RelWithDebInfo/hook"
		FOLDER "hook"
)
