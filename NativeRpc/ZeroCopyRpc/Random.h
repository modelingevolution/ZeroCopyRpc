#pragma once
#include <boost/uuid/random_generator.hpp>
#include <export.h>

struct EXPORT Random
{
    static boost::uuids::random_generator Shared;
    static unsigned long NextUlong();
};
