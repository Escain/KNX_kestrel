kestrel_add_library(appconfig
	SOURCES
		src/DeviceImages.cpp
		src/DownloadPlan.cpp
		src/Evaluator.cpp
		src/Images.cpp
		src/Import.cpp
		src/ParameterCodec.cpp
	PUBLIC_DEPS kestrel::catalog kestrel::model kestrel::download kestrel::base Qt6::Core
)
