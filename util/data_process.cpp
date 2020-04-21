#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>

int main(int args, char **argv) {
  std::ifstream fin("data/lineitem.txt", std::ios::in);
  std::ofstream ofresult("data/result.txt ", std::ios::trunc | std::ios::out);

  char line[1024] = {0};
  char tmp[33] = {0};

  while (fin.getline(line, sizeof(line))) {
    int cnt = 0, index = 0;
    while (cnt < 10) {
      if (line[index] == '|') {
        cnt++;
      }
      index++;
    }
    strncpy(tmp, line + index, 32);
    ofresult << tmp << '\n';
  }
  fin.clear();
  fin.close();
  ofresult.clear();
  ofresult.close();
  return 0;
}