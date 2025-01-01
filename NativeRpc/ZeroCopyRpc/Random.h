#pragma once
#include <boost/uuid/random_generator.hpp>
#include <Export.h>

struct EXPORT Random
{
    static boost::uuids::random_generator Shared;
    static unsigned long NextUlong();
};
