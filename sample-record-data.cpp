
#include <vector>
using namespace std;
int main() {
	vector<int> a;
	a.resize(0x1234567);
	for (int i=0; i<100000000; ++i)
	{
		for (unsigned int j=i % 0x1234567; a[j] < 100; j = ((unsigned long)j * 131UL + 25) % 0x1234567)
			++a[j];
	}
	return a[0x234567];
}
