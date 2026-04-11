#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <cstdint>

enum class Suit : uint8_t { SPADES, HEARTS, DIAMONDS, CLUBS };
enum class Rank : uint8_t { 
    TWO=2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE 
};
enum class HandRank : uint8_t {
    HIGH_CARD, ONE_PAIR, TWO_PAIR, THREE_OF_A_KIND, STRAIGHT, FLUSH, FULL_HOUSE, FOUR_OF_A_KIND, STRAIGHT_FLUSH, ROYAL_FLUSH
}

struct Card {
    Rank rank;
    Suit suit;
    bool operator==(const Card& o) const {
        return rank == o.rank && suit == o.suit;
    }
};

struct HandScore {
    HandRank rank;
    std::array<int, 5> kickers;
    bool operator>(const HandScore& o) const {
        if (rank != o.rank) return rank > o.rank;
        return kickers > o.kickers;
    }
    bool operator==(const HandScore& o) const {
        return rank == o.rank && kickers == o.kickers;
    }
};

Rank char_to_rank(char c) {
    switch(c) {
        case '2': return Rank::TWO;
        case '3': return Rank::THREE;
        case '4': return Rank::FOUR;
        case '5': return Rank::FIVE;
        case '6': return Rank::SIX;
        case '7': return Rank::SEVEN;
        case '8': return Rank::EIGHT;
        case '9': return Rank::NINE;
        case 'T': return Rank::TEN;
        case 'J': return Rank::JACK;
        case 'Q': return Rank::QUEEN;
        case 'K': return Rank::KING;
        case 'A': return Rank::ACE;
        default: return Rank::TWO;
    }
}

Suit char_to_suit(char c) {
    switch(c) {
        case 's': return Suit::SPADES;
        case 'h': return Suit::HEARTS;
        case 'd': return Suit::DIAMONDS;
        case 'c': return Suit::CLUBS;
        default: return Suit::SPADES;
    }
}

Card parse_card(const std::string& s) {
    return {char_to_rank(s[0]), char_to_suit(s[1])};
}

int main() {
    return 0;
}