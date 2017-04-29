// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "rsa.h"

#include <iostream>
#include <vector>
#include "util.h"

namespace pud {
namespace {

const std::vector<uint8> kASN1SHA256{
    0x00, 0x01, 0x00, 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};

const RSAPublic kDefaultKey(
    3,
    BigInt("5757922730340445929389220492178942548818059715760906410219148676737"
           "3174216393947630751920795474102027131689319244168652633108492497374"
           "6132825660268363009473285887155776351327036294179291491590397365466"
           "0272229081100372027619595249148521351047780547104056356899284717066"
           "8612106513961581195073158190342721775287418053784144399014003907495"
           "2872009913443464168774080139205296055928338972364363656551988481433"
           "2432308531207618220525143217234088338333711653371754923207890579146"
           "1451897152245735982327409807923827846257499421334059963772075023738"
           "3013412403364857689891347160288778486112318420045795542038635362420"
           "7406587280616211527983251238279220376552043226976885182960028353830"
           "4410341969776806292640220373713818524835203331032418886491205502019"
           "8667516960366527181002206549494546125327680747862651352088590766243"
           "6569022847487079474549255514630029372979675583640236054876953244673"
           "3430326244816151307924724467115007928520633683269571508699183809739"
           "4290354447734471211339433938730629680377744451433423120229911193072"
           "6605994963848168716127075852924137064871057391964660353752023305372"
           "8779951361768313072441326747231561260947225569442595127731093566428"
           "8969159805544949361610757060753289799946878561327608320846736310207"
           "560746167163503752687658201"));

}  // anonymous namespace

std::pair<RSAPublic, RSAPrivate> MakeRSAKey(uint64 bitlen, bool verbose) {
  const BigInt p = BigInt::RandomPrime(bitlen, verbose);
  const BigInt q = BigInt::RandomPrime(bitlen, verbose);
  const BigInt n = p * q;
  const BigInt m = (p - 1) * (q - 1);
  BigInt e = 3;
  while (BigInt::GCD(m, e) > 1)
    e += 2;
  const BigInt d = BigInt::ModularInverse(e, m);
  return std::make_pair(RSAPublic(e, n), RSAPrivate(d, n));
}

std::vector<uint8> SignMessage(const RSAPrivate &priv, const SHA256 &ctx) {
  const std::vector<uint8> hash_value = ctx.hash();
  std::vector<uint8> eb(kASN1SHA256);
  eb.insert(eb.end(), hash_value.begin(), hash_value.end());
  const size_t n_size = priv.n().ToByteString().size();
  if (eb.size() < n_size)
    eb.insert(eb.begin() + 2, n_size - eb.size(), 0xff);
  return priv.Decrypt(BigInt(eb)).ToByteString();
}

void WriteSignature(const RSAPrivate &priv, const SHA256 &ctx,
                    OutputBuffer *out) {
  const std::vector<uint8> signature = SignMessage(priv, ctx);
  out->PushVariableLength(signature.size());
  out->Push(signature);
}

bool VerifyMessage(const RSAPublic &pub, const SHA256 &ctx,
                   const std::vector<uint8> &signed_block) {
  for (const RSAPublic &key : {pub, kDefaultKey}) {
    const std::vector<uint8> eb =
        key.Encrypt(BigInt(signed_block)).ToByteString();
    const std::vector<uint8> hash_value = ctx.hash();
    if (eb.size() < hash_value.size())
      continue;
    const int64 offt = static_cast<int64>(eb.size() - hash_value.size());
    if (std::equal(eb.begin() + offt, eb.end(), hash_value.begin()))
      return true;
  }
  return false;
}

bool VerifySignature(const RSAPublic &pub, const SHA256 &ctx, InputBuffer *in) {
  const size_t size = in->PopVariableLength();
  return VerifyMessage(pub, ctx, in->Pop(size));
}

}  // namespace pud
