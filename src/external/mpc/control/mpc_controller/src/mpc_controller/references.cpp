// Copyright 2019 Christopher Ho
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Treat as a system header since we don't want to touch that autogenerated stuff..
#include <acado_common.h>
#include <motion_common/motion_common.hpp>

#include <algorithm>
#include <stdexcept>

#include "mpc_controller/mpc_controller.hpp"

namespace motion
{
namespace control
{
namespace mpc_controller
{

/// Typedef in namespace scope instead of macro--easier for debugging
using AcadoReal = real_t;

////////////////////////////////////////////////////////////////////////////////

constexpr auto HORIZON = static_cast<std::size_t>(ACADO_N);
constexpr auto NU = static_cast<std::size_t>(ACADO_NU);
// State variables
static_assert(ACADO_NX == 4, "Unexpected num of state variables");
constexpr auto NX = static_cast<std::size_t>(ACADO_NX);
// constexpr auto IDX_X = 0U;
// constexpr auto IDX_Y = 1U;
constexpr auto IDX_HEADING = 2U;
// constexpr auto IDX_VEL_LONG = 3U;
static_assert(ACADO_NYN == 4, "Unexpected number of terminal reference variables");
constexpr auto NYN = static_cast<std::size_t>(ACADO_NYN);
constexpr auto IDYN_X = 0U;
constexpr auto IDYN_Y = 1U;
constexpr auto IDYN_HEADING = 2U;
constexpr auto IDYN_VEL_LONG = 3U;
// Reference variable indices
static_assert(ACADO_NY == 4, "Unexpected number of reference variables");
constexpr auto NY = static_cast<std::size_t>(ACADO_NY);
constexpr auto IDY_X = 0U;
constexpr auto IDY_Y = 1U;
constexpr auto IDY_HEADING = 2U;
constexpr auto IDY_VEL_LONG = 3U;

////////////////////////////////////////////////////////////////////////////////
void MpcController::apply_weights(const OptimizationConfig & cfg)
{
  apply_nominal_weights(cfg.nominal(), Index{}, HORIZON);
  set_terminal_weights(cfg.terminal());
}
////////////////////////////////////////////////////////////////////////////////
void MpcController::zero_terminal_weights() noexcept
{
  static_assert(ACADO_NYN == 4, "Unexpected number of terminal reference variables");
  acadoVariables.WN[(IDYN_X * NYN) + IDYN_X] = AcadoReal{};
  acadoVariables.WN[(IDYN_Y * NYN) + IDYN_Y] = AcadoReal{};
  acadoVariables.WN[(IDYN_HEADING * NYN) + IDYN_HEADING] = AcadoReal{};
  acadoVariables.WN[(IDYN_VEL_LONG * NYN) + IDYN_VEL_LONG] = AcadoReal{};
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::set_terminal_weights(const StateWeight & cfg) noexcept
{
  static_assert(ACADO_NYN == 4, "Unexpected number of terminal reference variables");
  acadoVariables.WN[(IDYN_X * NYN) + IDYN_X] = static_cast<AcadoReal>(cfg.pose());
  acadoVariables.WN[(IDYN_Y * NYN) + IDYN_Y] = static_cast<AcadoReal>(cfg.pose());
  acadoVariables.WN[(IDYN_HEADING * NYN) + IDYN_HEADING] =
    static_cast<AcadoReal>(cfg.heading());
  acadoVariables.WN[(IDYN_VEL_LONG * NYN) + IDYN_VEL_LONG] =
    static_cast<AcadoReal>(cfg.longitudinal_velocity());
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::apply_nominal_weights(const StateWeight & cfg, const Index start, Index end)
{
  static_assert(sizeof(std::size_t) >= sizeof(Index), "static cast might truncate");
  end = std::min(static_cast<std::size_t>(end), HORIZON);
  if (start > end) {
    throw std::logic_error{"Inconsistent bounds: apply! There's likely an indexing bug somewhere"};
  }
  // 0 == hardcoded, 1 == variable, but time invariant, 2 == time varying
  static_assert(ACADO_WEIGHTING_MATRICES_TYPE == 2, "Weighting matrices should vary per timestep)");
  static_assert(ACADO_NY == 4, "Unexpected number of reference variables");
  for (Index i = start; i < end; ++i) {
    constexpr auto NY2 = NY * NY;
    const auto idx = i * NY2;
    acadoVariables.W[idx + (IDY_X * NY) + IDY_X] = static_cast<AcadoReal>(cfg.pose());
    acadoVariables.W[idx + (IDY_Y * NY) + IDY_Y] = static_cast<AcadoReal>(cfg.pose());
    acadoVariables.W[idx + (IDY_HEADING * NY) + IDY_HEADING] =
      static_cast<AcadoReal>(cfg.heading());
    acadoVariables.W[idx + (IDY_VEL_LONG * NY) + IDY_VEL_LONG] =
      static_cast<AcadoReal>(cfg.longitudinal_velocity());
  }
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::apply_terminal_weights(const Index idx)
{
  if (idx >= HORIZON) {
    throw std::logic_error{"Out of bounds terminal weight index"};
  }
  const auto & param = get_config().optimization_param().terminal();
  apply_nominal_weights(param, idx, idx + 1U);
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::zero_nominal_weights(const Index start, Index end)
{
  static_assert(sizeof(std::size_t) >= sizeof(Index), "static cast might truncate");
  end = std::min(HORIZON, static_cast<std::size_t>(end));
  if (start > end) {
    throw std::logic_error{"Inconsistent bounds: zero! There's likely an indexing bug somewhere"};
  }
  // 0 == hardcoded, 1 == variable, but time invariant, 2 == time varying
  static_assert(ACADO_WEIGHTING_MATRICES_TYPE == 2, "Weighting matrices should vary per timestep)");
  static_assert(ACADO_NY == 4, "Unexpected number of reference variables");
  constexpr auto NY2 = NY * NY;
  std::fill(&acadoVariables.W[start * NY2], &acadoVariables.W[end * NY2], AcadoReal{});
  // Zero initialization, above; std::fill preferred over memset for type safety
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::advance_problem(const Index count)
{
  if (HORIZON <= count) {
    throw std::logic_error{"Advancing count too high! Likely indexing bug somewhere"};
  }
  // TODO(c.ho) error checking on count
  // TODO(c.ho) x[0] should be x0... double check logic
  (void)std::copy(
    &acadoVariables.x[NX * (count + 1U)],
    &acadoVariables.x[(HORIZON + 1U) * NX],
    &acadoVariables.x[NX]);
  (void)std::copy(
    &acadoVariables.y[NY * count],
    &acadoVariables.y[HORIZON * NY],
    &acadoVariables.y[0U]);
  (void)std::copy(
    &acadoVariables.u[NU * count],
    &acadoVariables.u[HORIZON * NU],
    &acadoVariables.u[0U]);
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::backfill_reference(const Index count)
{
  static_assert(sizeof(std::size_t) >= sizeof(Index), "static cast might truncate");
  if (HORIZON <= count) {
    throw std::logic_error{"Backfill count too high! Likely indexing bug somewhere"};
  }
  // Start filling from count before the end
  const auto ref_start = HORIZON - count;
  // start pulling from the trajectory N - count from the current point
  const auto max_pts = static_cast<std::size_t>(get_reference_trajectory().points.size());
  const auto curr_idx = get_current_state_temporal_index();
  const auto traj_start = std::min(curr_idx + ref_start, max_pts);
  // Try to pull up to count
  const auto traj_end = std::min(traj_start + count, max_pts);
  const auto safe_count = traj_end - traj_start;
  // Pull references from trajectory
  set_reference(get_reference_trajectory(), ref_start, traj_start, safe_count);
  // Zero out remainder
  if (safe_count < count) {
    const auto remainder = count - safe_count;
    zero_nominal_weights(HORIZON - remainder, HORIZON);
    apply_terminal_weights(HORIZON - (remainder + 1));
  }
  // Set the terminal reference
  if (traj_start + count < max_pts) {
    set_terminal_reference(get_reference_trajectory().points[traj_start + count]);
  } else {
    zero_terminal_weights();
  }
}
////////////////////////////////////////////////////////////////////////////////
void MpcController::set_reference(
  const Trajectory & traj,
  const Index y_start,
  const Index traj_start,
  const Index count)
{
  if ((y_start + count) > HORIZON || (traj_start + count) > traj.points.size()) {
    throw std::logic_error{"set_reference would go out of bounds! Indexing bug likely!"};
  }
  for (auto i = Index{}; i < count; ++i) {
    const auto & pt = traj.points[traj_start + i];
    const auto ydx = (y_start + i) * NY;
    acadoVariables.y[ydx + IDY_X] = static_cast<AcadoReal>(pt.x);
    acadoVariables.y[ydx + IDY_Y] = static_cast<AcadoReal>(pt.y);
    acadoVariables.y[ydx + IDY_VEL_LONG] = static_cast<AcadoReal>(pt.longitudinal_velocity_mps);
    acadoVariables.y[ydx + IDY_HEADING] =
      static_cast<AcadoReal>(motion_common::to_angle(pt.heading));
  }
}

////////////////////////////////////////////////////////////////////////////////
bool MpcController::ensure_reference_consistency(Index horizon)
{
  static_assert(sizeof(std::size_t) >= sizeof(Index), "static cast might truncate");
  horizon = std::min(static_cast<std::size_t>(horizon), HORIZON);
  auto last_angle = acadoVariables.x0[IDX_HEADING];
  auto err = AcadoReal{};
  const auto fn = [&last_angle, &err](auto & ref) {
      const auto dth = ref - last_angle;
      const auto diff = std::atan2(std::sin(dth), std::cos(dth));
      const auto ref_old = ref;
      ref = last_angle + diff;
      err += std::fabs(ref - ref_old);
      last_angle = ref;
    };
  for (auto i = Index{}; i < horizon; ++i) {
    const auto idx = NY * i;
    fn(acadoVariables.y[idx + IDY_HEADING]);
  }
  fn(acadoVariables.yN[IDY_HEADING]);
  // Semi arbitrary number--maybe make it a config parameter?
  constexpr auto PI = AcadoReal{3.14159};
  return err > PI;
}

////////////////////////////////////////////////////////////////////////////////
void MpcController::set_terminal_reference(const Point & pt) noexcept
{
  acadoVariables.yN[IDYN_X] = static_cast<AcadoReal>(pt.x);
  acadoVariables.yN[IDYN_Y] = static_cast<AcadoReal>(pt.y);
  acadoVariables.yN[IDYN_VEL_LONG] = static_cast<AcadoReal>(pt.longitudinal_velocity_mps);
  acadoVariables.yN[IDYN_HEADING] = static_cast<AcadoReal>(motion_common::to_angle(pt.heading));
}

////////////////////////////////////////////////////////////////////////////////
const Trajectory & MpcController::handle_new_trajectory(const Trajectory & trajectory)
{
  static_assert(sizeof(std::size_t) >= sizeof(Index), "static cast might truncate");
  if (m_interpolated_trajectory) {
    using motion_common::sample;
    sample(trajectory, *m_interpolated_trajectory, solver_time_step);
  }
  const auto & traj = m_interpolated_trajectory ? *m_interpolated_trajectory : trajectory;
  const auto t_max = std::min(static_cast<std::size_t>(traj.points.size()), HORIZON);
  set_reference(traj, Index{}, Index{}, t_max);
  const auto & weights = get_config().optimization_param();
  apply_nominal_weights(weights.nominal(), Index{}, t_max);

  // Set terminal for infinite horizon control, and unset for finite horizon
  if (t_max < HORIZON) {
    // set remianing unused points from t_max to HORIZON as zero_nominal
    zero_nominal_weights(t_max, HORIZON);
  }
  // Set last reference point (with special weights) to one past whatever
  // the hardcoded optimization horizon is
  if (t_max >= traj.points.size()) {
    zero_terminal_weights();  // no terminal set
    apply_terminal_weights(traj.points.size() - 1U);
  } else {  // traj.points.size() > t_max
    // size is at least Horizon + 1  and tmax==HORIZON, used as reference
    set_terminal_reference(traj.points[t_max]);
  }
  m_last_reference_index = {};

  return traj;
}
}  // namespace mpc_controller
}  // namespace control
}  // namespace motion