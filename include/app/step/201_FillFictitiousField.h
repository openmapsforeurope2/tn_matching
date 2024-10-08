#ifndef _APP_STEP_FILLFICTITIOUSFIELD_H_
#define _APP_STEP_FILLFICTITIOUSFIELD_H_

#include <epg/step/StepBase.h>
#include <app/params/ThemeParameters.h>

namespace app {
	namespace step {

		class FillFictitiousField : public epg::step::StepBase< app::params::ThemeParametersS > {

		public:

			/// \brief
			int getCode() { return 201; };

			/// \brief
			std::string getName() { return "FillFictitiousField"; };

			/// \brief
			void onCompute(bool);

			/// \brief
			void init();

		};

	}
}

#endif