#ifndef _APP_STEP_JUNCTIONMATCHING_H_
#define _APP_STEP_JUNCTIONMATCHING_H_

#include <epg/step/StepBase.h>
#include <app/params/ThemeParameters.h>

namespace app {
	namespace step {

		class JunctionMatching : public epg::step::StepBase< app::params::ThemeParametersS > {

		public:

			/// \brief
			int getCode() { return 202; };

			/// \brief
			std::string getName() { return "JunctionMatching"; };

			/// \brief
			void onCompute(bool);

			/// \brief
			void init();

		};

	}
}

#endif