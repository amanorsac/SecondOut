#include "TestFramework.h"

int main()
{
    for (auto& t : allTests())
    {
        std::printf ("[ RUN  ] %s\n", t.name.c_str());
        const int before = failureCount();
        t.fn();
        std::printf ("[ %s ] %s\n", failureCount() == before ? " OK " : "FAIL", t.name.c_str());
    }

    if (failureCount() == 0)
    {
        std::printf ("\nAll %zu tests passed.\n", allTests().size());
        return 0;
    }

    std::printf ("\n%d failure(s).\n", failureCount());
    return 1;
}
