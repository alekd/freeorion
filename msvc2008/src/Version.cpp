#include "../../util/Version.h"

namespace {
    static const std::string retval = "v0.3.14 [Rev 3545]";
}

const std::string& FreeOrionVersionString()
{
    return retval;
}
