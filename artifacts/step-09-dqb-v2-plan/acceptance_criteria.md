# DQB-v2 Acceptance Criteria

- W2 burst: DQB-v2 improves at least one key P99/P999 load point by more than
  5% over both M1_AQB_PM and current M2_DQB_PM, with migration_rate <= 0.05.
- W1 saturation: DQB-v2 is not meaningfully worse than B2_Reactive and keeps
  migration low or disabled through the saturation guard.
- W3 heavy-tail: the result must be explainable. A gain supports hybrid
  blocking-batch repair; no gain must be backed by no-migrate reasons showing
  sparse blocking is a batch-migration boundary.
- Heterogeneous scenarios: DQB-v2 must not silently repeat M1's rho=0.85
  budget-misalignment failure. Reservation and slow-node diagnostics must
  identify the mechanism.
- Control overhead: estimated control cost should remain below 20% of the
  check period in the main configuration.
- CloudLab replay: method ranking should not invert, and the knee point should
  shift by no more than one rho bucket after calibrated constants are applied.
