# Hardware Test Plan: Session $SESSION

**Platform:** $PLATFORM
**System:** $SYSTEM
**Date:** $DATE
**Tester:** ________________

## Pre-Test Setup

### Build Verification
- [ ] Binary compiled for $PLATFORM
- [ ] Binary transferred to Classic Mac
- [ ] Application launches without crash

### Environment
- [ ] Note MaxBlock() value: ________ bytes
- [ ] Network connected (Ethernet/LocalTalk)
- [ ] No other apps running

## Test Cases

### Test 1: $TEST_1_NAME

**Objective:** $TEST_1_OBJECTIVE

**Steps:**
1. $TEST_1_STEP_1
2. $TEST_1_STEP_2
3. $TEST_1_STEP_3

**Expected Result:** $TEST_1_EXPECTED

**Actual Result:** ________________________________________

**Pass/Fail:** [ ] Pass  [ ] Fail

---

### Test 2: $TEST_2_NAME

**Objective:** $TEST_2_OBJECTIVE

**Steps:**
1. $TEST_2_STEP_1
2. $TEST_2_STEP_2
3. $TEST_2_STEP_3

**Expected Result:** $TEST_2_EXPECTED

**Actual Result:** ________________________________________

**Pass/Fail:** [ ] Pass  [ ] Fail

---

### Test 3: $TEST_3_NAME

**Objective:** $TEST_3_OBJECTIVE

**Steps:**
1. $TEST_3_STEP_1
2. $TEST_3_STEP_2
3. $TEST_3_STEP_3

**Expected Result:** $TEST_3_EXPECTED

**Actual Result:** ________________________________________

**Pass/Fail:** [ ] Pass  [ ] Fail

---

## Post-Test Verification

### Memory Check
- [ ] Final MaxBlock() value: ________ bytes
- [ ] No memory leak (MaxBlock unchanged or larger)

### Stability
- [ ] No crashes during testing
- [ ] No error dialogs
- [ ] Application quits cleanly

### All Tests Executed
- [ ] All test cases above completed

## Summary

| Test | Result | Notes |
|------|--------|-------|
| Test 1 | | |
| Test 2 | | |
| Test 3 | | |

**Overall Result:** [ ] PASS  [ ] FAIL

**Notes:**
________________________________________
________________________________________
________________________________________

**Signed:** ________________  **Date:** ________________
