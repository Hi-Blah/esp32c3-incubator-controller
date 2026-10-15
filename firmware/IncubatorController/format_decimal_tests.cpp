#include "format_decimal.h"
namespace {
constexpr bool equal(const char *a,const char *b){for(size_t i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}
constexpr bool decimalChecks(){
  char text[32]={};
  if(!decimal::tenths(text,sizeof(text),271)||!equal(text,"27.1"))return false;
  if(!decimal::tenths(text,sizeof(text),0,true)||!equal(text,"0.0"))return false;
  if(!decimal::tenths(text,sizeof(text),201,true)||!equal(text,"-20.1"))return false;
  if(!decimal::tenths(text,sizeof(text),6048000)||!equal(text,"604800.0"))return false;
  if(!decimal::tenths(text,sizeof(text),UINT64_MAX)||!equal(text,"1844674407370955161.5"))return false;
  char shortText[4]={};
  if(decimal::tenths(shortText,sizeof(shortText),271)||shortText[0])return false;
  if(!decimal::tenths(shortText,sizeof(shortText),1)||!equal(shortText,"0.1"))return false;
  return true;
}
static_assert(decimalChecks(),"Bounded integer decimal formatting regression");
}
