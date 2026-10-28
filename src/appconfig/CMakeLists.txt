kestrel_add_library(appconfig
	SOURCES
		src/DeviceImages.cpp
		src/Evaluator.cpp
		src/Images.cpp
		src/ParameterCodec.cpp
	PUBLIC_DEPS kestrel::catalog kestrel::model kestrel::base Qt6::Core
)
