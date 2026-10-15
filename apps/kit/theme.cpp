#include "theme.h"

namespace stardraft::kit {

Theme& theme() {
    static Theme t;
    return t;
}

}  // namespace stardraft::kit
