#include <boost/multiprecision/cpp_int.hpp>
#include <iostream>
#include <ql/quantlib.hpp>

using namespace boost::multiprecision;

int main(int argc, char *argv[]) {
  long long num1 = 1523844560192817464;
  long long num2 = 598274671729184766;
  int128_t result = (int128_t)num1 * num2;
  std::cout << "The product of the two integers is:" << "\n" << result << "\n";
  QuantLib::Option::Type OptionType(QuantLib::Option::Call);
  std::cout << "Option Type = " << OptionType << std::endl;
  return 0;
}
