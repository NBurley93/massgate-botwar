add_executable(FakeDedicatedServer
	main.cpp
	stdafx.h
)

target_link_libraries(FakeDedicatedServer
	PRIVATE
		Lib::MCommon2
		Lib::MNetwork
		Lib::MMassgate
		Lib::MServerBox
		Lib::MLogger
		Lib::MDatabase
		wininet
)

set_target_properties(FakeDedicatedServer
	PROPERTIES
		CXX_STANDARD 98
		FOLDER "tools"
)
