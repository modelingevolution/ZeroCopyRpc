#include "TypeDefs.h"

#include "CyclicBuffer.hpp"
#include "Export.h"

template class EXPORT CyclicBuffer<8 * 1024 * 1024, 256>;
