#include "AES.h"
#include <cstring>
#include <algorithm>
#include <cstdint>

// S-Box, Inverse S-Box, Rcon... (Standard AES lookup tables)
static const std::uint8_t sbox[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const std::uint8_t rsbox[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

static const std::uint8_t Rcon[11] = {
  0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

AES::AES(KeyLength keyLength) {
    m_nk = keyLength / 4;
    m_nb = 4;
    if (keyLength == AES_128) m_nr = 10;
    else if (keyLength == AES_192) m_nr = 12;
    else m_nr = 14;
}

AES::~AES() {}

void AES::keyExpansion(const std::uint8_t* key, std::uint8_t* w) {
    std::uint8_t temp[4];
    for (int i = 0; i < m_nk; ++i) {
        w[4 * i] = key[4 * i];
        w[4 * i + 1] = key[4 * i + 1];
        w[4 * i + 2] = key[4 * i + 2];
        w[4 * i + 3] = key[4 * i + 3];
    }
    for (int i = m_nk; i < m_nb * (m_nr + 1); ++i) {
        temp[0] = w[4 * (i - 1)];
        temp[1] = w[4 * (i - 1) + 1];
        temp[2] = w[4 * (i - 1) + 2];
        temp[3] = w[4 * (i - 1) + 3];
        if (i % m_nk == 0) {
            std::uint8_t t = temp[0];
            temp[0] = sbox[temp[1]] ^ Rcon[i / m_nk];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
        } else if (m_nk > 6 && i % m_nk == 4) {
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];
        }
        w[4 * i] = w[4 * (i - m_nk)] ^ temp[0];
        w[4 * i + 1] = w[4 * (i - m_nk) + 1] ^ temp[1];
        w[4 * i + 2] = w[4 * (i - m_nk) + 2] ^ temp[2];
        w[4 * i + 3] = w[4 * (i - m_nk) + 3] ^ temp[3];
    }
}

static std::uint8_t gmul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        bool hi_bit_set = (a & 0x80);
        a <<= 1;
        if (hi_bit_set) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

void AES::cipher(const std::uint8_t in[16], std::uint8_t out[16], const std::uint8_t* w) {
    std::uint8_t state[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            state[j][i] = in[i * 4 + j] ^ w[i * 4 + j];

    for (int round = 1; round < m_nr; ++round) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                state[i][j] = sbox[state[i][j]];

        std::uint8_t temp = state[1][0];
        state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = temp;
        temp = state[2][0]; std::uint8_t temp2 = state[2][1];
        state[2][0] = state[2][2]; state[2][1] = state[2][3]; state[2][2] = temp; state[2][3] = temp2;
        temp = state[3][0]; temp2 = state[3][1]; std::uint8_t temp3 = state[3][2];
        state[3][0] = state[3][3]; state[3][1] = temp; state[3][2] = temp2; state[3][3] = temp3;

        for (int j = 0; j < 4; ++j) {
            std::uint8_t a = state[0][j], b = state[1][j], c = state[2][j], d = state[3][j];
            state[0][j] = gmul(a, 2) ^ gmul(b, 3) ^ c ^ d;
            state[1][j] = a ^ gmul(b, 2) ^ gmul(c, 3) ^ d;
            state[2][j] = a ^ b ^ gmul(c, 2) ^ gmul(d, 3);
            state[3][j] = gmul(a, 3) ^ b ^ c ^ gmul(d, 2);
        }

        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                state[j][i] ^= w[round * 16 + i * 4 + j];
    }

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            state[i][j] = sbox[state[i][j]];

    std::uint8_t temp = state[1][0];
    state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = temp;
    temp = state[2][0]; std::uint8_t temp2 = state[2][1];
    state[2][0] = state[2][2]; state[2][1] = state[2][3]; state[2][2] = temp; state[2][3] = temp2;
    temp = state[3][0]; temp2 = state[3][1]; std::uint8_t temp3 = state[3][2];
    state[3][0] = state[3][3]; state[3][1] = temp; state[3][2] = temp2; state[3][3] = temp3;

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = state[j][i] ^ w[m_nr * 16 + i * 4 + j];
}

void AES::invCipher(const std::uint8_t in[16], std::uint8_t out[16], const std::uint8_t* w) {
    std::uint8_t state[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            state[j][i] = in[i * 4 + j] ^ w[m_nr * 16 + i * 4 + j];

    for (int round = m_nr - 1; round > 0; --round) {
        std::uint8_t temp = state[1][3];
        state[1][3] = state[1][2]; state[1][2] = state[1][1]; state[1][1] = state[1][0]; state[1][0] = temp;
        temp = state[2][2]; std::uint8_t temp2 = state[2][3];
        state[2][2] = state[2][0]; state[2][3] = state[2][1]; state[2][0] = temp; state[2][1] = temp2;
        temp = state[3][0]; temp2 = state[3][1]; std::uint8_t temp3 = state[3][2];
        state[3][0] = state[3][1]; state[3][1] = state[3][2]; state[3][2] = state[3][3]; state[3][3] = temp;

        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                state[i][j] = rsbox[state[i][j]];

        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                state[j][i] ^= w[round * 16 + i * 4 + j];

        for (int j = 0; j < 4; ++j) {
            std::uint8_t a = state[0][j], b = state[1][j], c = state[2][j], d = state[3][j];
            state[0][j] = gmul(a, 14) ^ gmul(b, 11) ^ gmul(c, 13) ^ gmul(d, 9);
            state[1][j] = gmul(a, 9) ^ gmul(b, 14) ^ gmul(c, 11) ^ gmul(d, 13);
            state[2][j] = gmul(a, 13) ^ gmul(b, 9) ^ gmul(c, 14) ^ gmul(d, 11);
            state[3][j] = gmul(a, 11) ^ gmul(b, 13) ^ gmul(c, 9) ^ gmul(d, 14);
        }
    }

    std::uint8_t temp = state[1][3];
    state[1][3] = state[1][2]; state[1][2] = state[1][1]; state[1][1] = state[1][0]; state[1][0] = temp;
    temp = state[2][2]; std::uint8_t temp2 = state[2][3];
    state[2][2] = state[2][0]; state[2][3] = state[2][1]; state[2][0] = temp; state[2][1] = temp2;
    temp = state[3][0]; temp2 = state[3][1]; std::uint8_t temp3 = state[3][2];
    state[3][0] = state[3][1]; state[3][1] = state[3][2]; state[3][2] = state[3][3]; state[3][3] = temp;

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            state[i][j] = rsbox[state[i][j]];

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = state[j][i] ^ w[i * 4 + j];
}

std::vector<std::uint8_t> AES::encryptCBC(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv) {
    std::vector<std::uint8_t> w(m_nb * (m_nr + 1) * 4);
    keyExpansion(key.data(), w.data());

    // PKCS#7 Padding
    std::size_t paddingLen = 16 - (input.size() % 16);
    std::vector<std::uint8_t> paddedInput = input;
    for (std::size_t i = 0; i < paddingLen; ++i) paddedInput.push_back((std::uint8_t)paddingLen);

    std::vector<std::uint8_t> output(paddedInput.size());
    std::uint8_t prevBlock[16];
    std::memcpy(prevBlock, iv.data(), 16);

    for (std::size_t i = 0; i < paddedInput.size(); i += 16) {
        std::uint8_t block[16];
        for (int j = 0; j < 16; ++j) block[j] = paddedInput[i + j] ^ prevBlock[j];
        cipher(block, &output[i], w.data());
        std::memcpy(prevBlock, &output[i], 16);
    }
    return output;
}

std::vector<std::uint8_t> AES::decryptCBC(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv) {
    if (input.empty() || input.size() % 16 != 0) return {};
    std::vector<std::uint8_t> w(m_nb * (m_nr + 1) * 4);
    keyExpansion(key.data(), w.data());

    std::vector<std::uint8_t> output(input.size());
    std::uint8_t prevBlock[16];
    std::memcpy(prevBlock, iv.data(), 16);

    for (std::size_t i = 0; i < input.size(); i += 16) {
        std::uint8_t block[16];
        invCipher(&input[i], block, w.data());
        for (int j = 0; j < 16; ++j) output[i + j] = block[j] ^ prevBlock[j];
        std::memcpy(prevBlock, &input[i], 16);
    }

    // PKCS#7 Unpadding
    std::uint8_t paddingLen = output.back();
    if (paddingLen > 0 && paddingLen <= 16) {
        bool valid = true;
        for (std::size_t i = 0; i < paddingLen; ++i) {
            if (output[output.size() - 1 - i] != paddingLen) { valid = false; break; }
        }
        if (valid) output.resize(output.size() - paddingLen);
    }
    return output;
}
