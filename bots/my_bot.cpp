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

std::vector<Card> full_deck() {
    std::vector<Card> d;
    d.reserve(52);
    for (int s = 0; s < 4; s++) {
        for (int r = 2; r <= 14; r++) {
            d.push_back({static_cast<Rank>(r), static_cast<Suit>(s)});
        }
    }
    return d;
}

bool card_in(const Card& c, const std::vector<Card>& v) {
    for (const Card& x : v) {
        if (c == x) return true;
    }
    return false;
}

std::vector<Card> build_unseen(const std::array<Card, 2>& hole,
    const std::vector<Card>& community,
    const std::vector<Card>& discarded) {
    std::vector<Card> unseen;
    for (const Card& c : full_deck()) {
        if (c == hole[0] || c == hole[1]) continue;
        if (card_in(c, community)) continue;
        if (card_in(c, discarded)) continue;
        unseen.push_back(c);
    }
    return unseen;
}

static std::mt19937 rng(
    std::chrono::steady_clock::now().time_since_epoch().count());

float estimate_equity(const std::array<Card, 2>& hole,
    const std::vector<Card>& community,
    const std::vector<Card>& discarded,
    int num_opponents,
    int max_sims = 3000,
    int time_budget_us = 5500) {
    
    if (num_opponents <= 0 ) return 1.0f;

    auto unseen = build_unseen(hole, community, discarded);
    int n = (int)unseen.size();
    int cards_needed = (5 - (int)community.size()) + 2 * num_opponents;
    if (n < cards_needed) return 1.0f;

    auto start = std::chrono::steady_clock::now();
    float score_sum = 0.0f;
    int sims = 0;

    for (sims = 0; sims < max_sims; sims++) {
        // Check time budget every 64 simulations, with bitwise operations
        if (sims > 0 && (sims & 63) == 0) {
            auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed >= time_budget_us) break;
        }
        
        // Partial Fisher-Yates shuffle to get random cards
        for (int i = 0; i < cards_needed; i++) {
            int j = i + (rng() % (n - i));
            std::swap(unseen[i], unseen[j]);
        }

        int idx = 0;
        // Complete community
        std::vector<Card> sim_comm = community;
        while ((int)sim_comm.size() < 5)
            sim_comm.push_back(unseen[idx++]);
            
        // check own hand
        std::vector<Card> my_cards(sim_comm.begin(), sim_comm.end());
        my_cards.push_back(hole[0]);
        my_cards.push_back(hole[1]);
        HandScore mine = evaluate_hand(my_cards);

        // check opponents, count ties
        bool i_win = true;
        int tie_count = 0;
        for (int o = 0; o < num_opponents; o++) {
            std::vector<Card> opp(sim_comm.begin(), sim_comm.end());
            opp.push_back(unseen[idx++]);
            opp.push_back(unseen[idx++]);
            HandScore s = evaluate_hand(opp);
            if (s > mine) {
                i_win = false;
                break;
            }
            if (s == mine) tie_count++;
        }
        if (i_win) {
            // 1.0 for win, 1/(tie_count+1) for tie
            score_sum += 1.0f / (tie_count + 1);
        }
    }
    if (sims == 0) return 0.5f;
    std::cout << "Ran " << sims << " simulations in " 
         << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count() << " ms" << std::endl;
    return score_sum / sims;
}

int main() {
    std::vector<std::string> test_hands = {
        "As Ks Qs Js Ts",       // Royal Flush
        "7h 7d 7s 7c Kd",       // Four of a Kind
        "Ah Ad As Kh Kd",       // Full House
        "2s 4s 6s 8s Ts",       // Flush
        "Ad 2h 3c 4s 5d",       // Straight (Wheel)
        "9h 9c 9d 2s 3c",       // Three of a Kind
        "Jh Jc Th Tc 2s",       // Two Pair
        "8h 8c Ad Ks Qc",       // One Pair
        "Ah Jc 9d 5s 3h",       // High Card
        "As Ks Qs Js Ts 9s 8s"  // 7 cards, should pick best 5 (Royal Flush)
    };
    // for (const std::string& hand_str : test_hands) {
    //     std::istringstream iss(hand_str);
    //     std::vector<Card> cards;
    //     std::string card_str;
    //     while (iss >> card_str) {
    //         cards.push_back(parse_card(card_str));
    //     }
    //     HandScore score = evaluate_hand(cards);
    //     std::cout << "Hand: " << hand_str << " -> Rank: " << static_cast<int>(score.rank) << " Kickers: ";
    //     for (int k : score.kickers) {
    //         if (k > 0) std::cout << k << " ";
    //     }
    //     std::cout << std::endl;
    // }

    // Example equity estimation 1: Trash hand post-flop
    std::array<Card, 2> hole1 = {parse_card("2h"), parse_card("3s")};
    std::vector<Card> comm1 = {parse_card("7h"), parse_card("5s"), parse_card("Qd")};
    std::vector<Card> disc = {};
    float eq1 = estimate_equity(hole1, comm1, disc, 2);
    std::cout << "Equity (2h 3s on 7h 5s Qd vs 2): " << eq1 << std::endl;

    // Example equity estimation 2: Pocket Aces pre-flop
    std::array<Card, 2> hole2 = {parse_card("As"), parse_card("Ah")};
    std::vector<Card> comm2 = {};
    float eq2 = estimate_equity(hole2, comm2, disc, 1);
    std::cout << "Equity (As Ah pre-flop vs 1): " << eq2 << std::endl;

    // Example equity estimation 3: Flush draw on the flop
    std::array<Card, 2> hole3 = {parse_card("Ks"), parse_card("Qs")};
    std::vector<Card> comm3 = {parse_card("2s"), parse_card("9s"), parse_card("4d")};
    float eq3 = estimate_equity(hole3, comm3, disc, 1);
    std::cout << "Equity (Ks Qs on 2s 9s 4d vs 1): " << eq3 << std::endl;

    return 0;
}