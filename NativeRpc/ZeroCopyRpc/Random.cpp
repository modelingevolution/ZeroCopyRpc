#include "Random.h"

boost::uuids::random_generator Random::Shared;

unsigned long Random::NextUlong()
{
    std::random_device rd;
    // Use a Mersenne Twister engine for generating random numbers
    std::mt19937_64 gen(rd());
    // Define the range for unsigned long
    std::uniform_int_distribution<unsigned long> dist(0, 0xffffffffUL);
    // Generate a random unsigned long
    unsigned long randomValue = dist(gen);
    return randomValue;
}
