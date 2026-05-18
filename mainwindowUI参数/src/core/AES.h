#ifndef AES_H
#define AES_H

#include <vector>
#include <cstdint>

class AES {
public:
    enum KeyLength { AES_128 = 16, AES_192 = 24, AES_256 = 32 };

    explicit AES(KeyLength keyLength);
    ~AES();

    // CBC 加密
    std::vector<std::uint8_t> encryptCBC(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv);
    // CBC 解密
    std::vector<std::uint8_t> decryptCBC(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv);

private:
    void cipher(const std::uint8_t in[16], std::uint8_t out[16], const std::uint8_t* w);
    void invCipher(const std::uint8_t in[16], std::uint8_t out[16], const std::uint8_t* w);
    void keyExpansion(const std::uint8_t* key, std::uint8_t* w);
    
    int m_nb;
    int m_nk;
    int m_nr;
};

#endif // AES_H
