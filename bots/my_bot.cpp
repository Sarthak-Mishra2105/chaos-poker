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
};

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

HandScore evaluate_five(const std::array<Card, 5>& cards) {
    std::array<int, 5> vals, suits;
    for (int i = 0; i < 5; i++){
        vals[i] = static_cast<int>(cards[i].rank);
        suits[i] = static_cast<int>(cards[i].suit);
    }

    // Sort vals in descending order
    std::sort(vals.begin(), vals.end(), std::greater<int>());

    // Check for flush
    bool is_flush = (suits[0] == suits[1] && 
        suits[1] == suits[2] && 
        suits[2] == suits[3] && 
        suits[3] == suits[4]
    );
    
    // Check for straight
    bool is_straight = false;
    int straight_high = 0;
    if (vals[0] - vals[4] == 4 &&
        vals[0] != vals[1] &&
        vals[1] != vals[2] &&
        vals[2] != vals[3] &&
        vals[3] != vals[4]) {
        is_straight = true;
        straight_high = vals[0];
    }

    // Wheel straight (A-2-3-4-5)
    if (vals[0] == 14 && vals[1] == 5 &&
        vals[2] == 4 && vals[3] == 3 && vals[4] == 2) {
        is_straight = true;
        straight_high = 5;
    }

    // Count frequency of each rank
    int count[15] = {0};
    for (int v : vals) count[v]++;

    int four_val = 0, three_val = 0, pairs[2] = {}, pair_count = 0;
    for (int v = 14; v >= 2; v--) {
        if (count[v] == 4) four_val = v;
        else if (count[v] == 3) three_val = v;
        else if (count[v] == 2 && pair_count < 2) pairs[pair_count++] = v;
    }

    // Determine hand rank
    HandScore hs;
    hs.kickers = {};

    if (is_straight && is_flush) {
        hs.rank = (straight_high == 14) ? HandRank::ROYAL_FLUSH : HandRank::STRAIGHT_FLUSH;
        hs.kickers[0] = straight_high;
    }
    else if (four_val) {
        hs.rank = HandRank::FOUR_OF_A_KIND;
        hs.kickers[0] = four_val;
        for (int v : vals) {
            if (v != four_val) {
                hs.kickers[1] = v;
                break;
            }
        }
    }

    else if (three_val && pair_count >= 1) {
        hs.rank = HandRank::FULL_HOUSE;
        hs.kickers[0] = three_val;
        hs.kickers[1] = pairs[0];
    }

    else if (is_flush) {
        hs.rank = HandRank::FLUSH;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    }
    
    else if (is_straight) {
        hs.rank = HandRank::STRAIGHT;
        hs.kickers[0] = straight_high;
    }

    else if (three_val) {
        hs.rank = HandRank::THREE_OF_A_KIND;
        hs.kickers[0] = three_val;
        int ki = 1;
        for (int v : vals) {
            if (v != three_val) {
                hs.kickers[ki++] = v;
            }
        }
    }

    else if (pair_count == 2) {
        hs.rank = HandRank::TWO_PAIR;
        hs.kickers[0] = pairs[0];
        hs.kickers[1] = pairs[1];
        for (int v : vals) {
            if (v != pairs[0] && v != pairs[1]) {
                hs.kickers[2] = v;
                break;
            }
        }
    }

    else if (pair_count == 1) {
        hs.rank = HandRank::ONE_PAIR;
        hs.kickers[0] = pairs[0];
        int ki = 1;
        for (int v : vals) {
            if (v != pairs[0]) {
                hs.kickers[ki++] = v;
            }
        }
    }

    else {
        hs.rank = HandRank::HIGH_CARD;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    }

    return hs;
}

HandScore evaluate_hand(const std::vector<Card>& cards) {
    int n = (int)cards.size();
    HandScore best;
    best.rank = HandRank::HIGH_CARD;
    best.kickers = {};
    bool first = true;

    for (int a = 0; a < n-4; a++) {
        for (int b = a+1; b < n-3; b++) {
            for (int c = b+1; c < n-2; c++) {
                for (int d = c+1; d < n-1; d++) {
                    for (int e = d+1; e < n; e++) {
                        std::array<Card, 5> hand = {cards[a], cards[b], cards[c], cards[d], cards[e]};
                        HandScore hs = evaluate_five(hand);
                        if (first || hs > best) {
                            best = hs;
                            first = false;
                        }
                    }
                }
            }
        }
    }
    return best;
}

int main() {
    std::vector<std::string> test_hands = {
        "As Ks Qs Js Ts",
        "7h 7d 7s 7c Kd",
        "Ad 2h 3c 4s 5d"
    };
    for (const std::string& hand_str : test_hands) {
        std::istringstream iss(hand_str);
        std::vector<Card> cards;
        std::string card_str;
        while (iss >> card_str) {
            cards.push_back(parse_card(card_str));
        }
        HandScore score = evaluate_hand(cards);
        std::cout << "Hand: " << hand_str << " -> Rank: " << static_cast<int>(score.rank) << " Kickers: ";
        for (int k : score.kickers) {
            if (k > 0) std::cout << k << " ";
        }
        std::cout << std::endl;
    }
    return 0;
}