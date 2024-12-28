#pragma once
#include <boost/uuid/random_generator.hpp>

struct Random
{
    static boost::uuids::random_generator Shared;
};
