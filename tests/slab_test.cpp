#include <gtest/gtest.h>
#include <io/Slab.hpp>

TEST(SlabTest, InsertGetRemoveOne)
{
  auto slab = Slab<int> {};
  ASSERT_TRUE(slab.isEmpty());
  auto key = slab.insert(123);
  ASSERT_EQ(slab[key], 123);
  ASSERT_EQ(*slab.get(key), 123);
  ASSERT_FALSE(slab.isEmpty());
  ASSERT_TRUE(slab.contains(key));

  auto v = slab.tryRemove(key);
  ASSERT_TRUE(v.has_value());
  ASSERT_EQ(v.value(), 123);
  ASSERT_FALSE(slab.contains(key));
  ASSERT_TRUE(slab.get(key) == nullptr);
}

TEST(SlabTest, InsertGetMany)
{
  auto slab = Slab<int> {};

  for (int i = 0; i < 10; i++) {
    auto key = slab.insert(i + 10);
    ASSERT_EQ(slab[key], i + 10);
  }

  ASSERT_EQ(slab.len(), 10);
  auto key = slab.insert(123);
  ASSERT_EQ(slab[key], 123);

  ASSERT_EQ(slab.len(), 11);
}

TEST(SlabTest, InsertGetRemoveMany)
{
  auto slab = Slab<int> {};
  auto keys = std::vector<std::pair<size_t, int>> {};

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      auto val = (i * 10) + j + 12345;
      auto key = slab.insert(std::move(val));
      keys.emplace_back(key, val);
      ASSERT_EQ(slab[key], val);
    }

    for (auto [key, val] : keys) {
      ASSERT_EQ(slab[key], val);
      auto res = slab.tryRemove(key);
      ASSERT_TRUE(res.has_value());
      ASSERT_EQ(res.value(), val);
    }
    keys.clear();
  }
}

TEST(SlabTest, InsertWithVacantEntry)
{
  auto slab = Slab<int> {};
  {
    auto entry = slab.vacantEntry();
    auto key = entry.key;
    entry.insert(123);
    ASSERT_EQ(123, slab[key]);
  }
}

TEST(SlabTest, GetVacantEntry)
{
  auto slab = Slab<int> {};
  auto key = slab.vacantEntry().key;
  ASSERT_EQ(key, slab.vacantEntry().key);
}