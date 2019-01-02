#pragma once

// I generously include all external headers I found in the project header files,
// including std, boost and GG headers.

// https://hownot2code.com/2016/08/16/stdafx-h/


#include <bitset>
#include <cmath>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/expressions/keyword.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/optional/optional.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/uniform_01.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/uniform_smallint.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/shared_array.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/statechart/event.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/uuid/uuid.hpp>

// Note: The is a workaround for Visual C++ non-conformant pre-processor
// handling of empty macro arguments.
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/facilities/is_empty_variadic.hpp>


#include <GG/GGFwd.h>
#include <GG/Base.h>
#include <GG/BrowseInfoWnd.h>
#include <GG/Button.h>
#include <GG/Clr.h>
#include <GG/Control.h>
#include <GG/DropDownList.h>
#include <GG/DynamicGraphic.h>
#include <GG/Edit.h>
#include <GG/Enum.h>
#include <GG/GGFwd.h>
#include <GG/GLClientAndServerBuffer.h>
#include <GG/GUI.h>
#include <GG/Layout.h>
#include <GG/ListBox.h>
#include <GG/ListBox.h>
#include <GG/Menu.h>
#include <GG/MultiEdit.h>
#include <GG/PtRect.h>
#include <GG/PtRect.h>
#include <GG/RichText/BlockControl.h>
#include <GG/RichText/RichText.h>
#include <GG/SDL/SDLGUI.h>
#include <GG/Scroll.h>
#include <GG/Slider.h>
#include <GG/Slider.h>
#include <GG/Spin.h>
#include <GG/StaticGraphic.h>
#include <GG/StyleFactory.h>
#include <GG/TabWnd.h>
#include <GG/Texture.h>
#include <GG/Wnd.h>
#include <GG/Wnd.h>
#include <GG/WndEvent.h>
#include <GG/dialogs/FileDlg.h>
