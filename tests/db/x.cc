#include "../catch.hpp"
#include<stdio.h>
#include <db/schema.h>
using namespace db;

TEST_CASE("X")//随便改个名字
{
    SECTION("init")//随便写个名字
    {
        REQUIRE(true);//返回真
        printf("done");
    }
}