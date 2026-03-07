#pragma once

class IFillListener {
public:
  virtual ~IFillListener() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
};
