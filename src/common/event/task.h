#pragma once

#include <uv.h>
#include <functional>
#include <memory>
#include <utility>

#include "src/common/base/base.h"

namespace pl {
namespace event {

/**
 * AsyncTask is an interface for tasks that we can run on the threadpool.
 */
class AsyncTask {
 public:
  virtual ~AsyncTask() = default;
  /**
   * Work is run on a threadpool.
   */
  virtual void Work() = 0;
  /**
   * Done is called on the event thread after exection of Work() is complete.
   * This function should not preform any blocking or compute intensive
   * operations.
   */
  virtual void Done() = 0;
};
using AsyncTaskUPtr = std::unique_ptr<AsyncTask>;

/**
 * RunnableAsyncTask is a wrapper around an AsyncTask.
 * The lifetime of the contained task must execeed the lifetime of this class.
 * TODO(zasgar): Explore using packaged tasks.
 */
class RunnableAsyncTask {
 public:
  explicit RunnableAsyncTask(AsyncTask* task) : task_(task) {}
  virtual ~RunnableAsyncTask() = default;

  virtual void Run() = 0;

 protected:
  AsyncTask* task_;
};
using RunnableAsyncTaskUPtr = std::unique_ptr<RunnableAsyncTask>;

}  // namespace event
}  // namespace pl
