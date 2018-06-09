/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "ssp.h"
#include <linux/math64.h>
#include <linux/string.h>

#define BATCH_IOCTL_MAGIC		0xFC

struct batch_config {
	int64_t timeout;
	int64_t delay;
	int flag;
};

/*************************************************************************/
/* SSP data delay function                                              */
/*************************************************************************/

int get_msdelay(int64_t dDelayRate)
{
	return div_s64(dDelayRate, 1000000);
}

static void enable_sensor(struct ssp_data *data,
	int iSensorType, int64_t dNewDelay)
{
	u8 uBuf[9];
	u64 uNewEnable = 0;
	s32 maxBatchReportLatency = 0;
	s8 batchOptions = 0;
	int64_t dTempDelay = data->adDelayBuf[iSensorType];
	s32 dMsDelay = get_msdelay(dNewDelay);
	int ret = 0;

	// SUPPORT CAMERA SYNC ++++++
	if (iSensorType == GYROSCOPE_SENSOR) {
		if (dNewDelay == CAMERA_GYROSCOPE_SYNC) {
			dNewDelay = CAMERA_GYROSCOPE_SYNC_DELAY;
			dMsDelay = get_msdelay(dNewDelay);
			data->cameraGyroSyncMode = true;
		} else if(dNewDelay == CAMERA_GYROSCOPE_VDIS_SYNC){
			dNewDelay = CAMERA_GYROSCOPE_VDIS_SYNC_DELAY;
			dMsDelay = get_msdelay(dNewDelay);
			data->cameraGyroSyncMode = true;
		} else {
			data->cameraGyroSyncMode = false;
			if((data->adDelayBuf[iSensorType] == dNewDelay) && (data->aiCheckStatus[iSensorType] == RUNNING_SENSOR_STATE)) {
				pr_err("[SSP] same delay ignored! \n");
				return;
			}
		}

		if((data->cameraGyroSyncMode == true) && (data->aiCheckStatus[iSensorType] == RUNNING_SENSOR_STATE)){
			if(data->adDelayBuf[iSensorType] == dNewDelay)
				return;
		}
	}
	// SUPPORT CAMERA SYNC -----

	data->adDelayBuf[iSensorType] = dNewDelay;
	maxBatchReportLatency = data->batchLatencyBuf[iSensorType];
	batchOptions = data->batchOptBuf[iSensorType];

	switch (data->aiCheckStatus[iSensorType]) {
	case ADD_SENSOR_STATE:
		ssp_dbg("[SSP]: %s - add %u, New = %lldns\n",
			 __func__, 1 << iSensorType, dNewDelay);

		if (iSensorType == PROXIMITY_SENSOR) {
#ifdef CONFIG_SENSORS_SSP_PROX_FACTORYCAL
			get_proximity_threshold(data);
			proximity_open_calibration(data);
#endif
			set_proximity_threshold(data);
			data->sensor_dump_flag_proximity = true;
		}

		if (iSensorType == PROXIMITY_ALERT_SENSOR) {
			set_proximity_alert_threshold(data);
		}

#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
		if(iSensorType == LIGHT_SENSOR || iSensorType == LIGHT_IR_SENSOR) {
			data->light_log_cnt = 0;
			data->light_ir_log_cnt = 0;

		}
#else
		if(iSensorType == LIGHT_SENSOR) {
			data->light_log_cnt = 0;
		}
#endif
		if(iSensorType == LIGHT_SENSOR)
		{
			data->sensor_dump_cnt_light = 0;
			data->sensor_dump_flag_light = true;
		}

#ifdef CONFIG_SENSORS_SSP_SX9306
		if (iSensorType == GRIP_SENSOR) {
			open_grip_caldata(data);
			set_grip_calibration(data, true);
		}
#endif

		memcpy(&uBuf[0], &dMsDelay, 4);
		memcpy(&uBuf[4], &maxBatchReportLatency, 4);
		uBuf[8] = batchOptions;

		if (iSensorType == TEMPERATURE_HUMIDITY_SENSOR &&
			dMsDelay == 10000)
			pr_info("[SSP] Skip Add Temphumidity Sensor\n");
		else
			ret = send_instruction(data, ADD_SENSOR,
				iSensorType, uBuf, 9);
		pr_info("[SSP], delay %d, timeout %d, flag=%d, ret%d \n",
			dMsDelay, maxBatchReportLatency, uBuf[8], ret);
		if (ret <= 0) {
			uNewEnable =
				(u64)atomic64_read(&data->aSensorEnable)
				& (~(u64)(1 << iSensorType));
			atomic64_set(&data->aSensorEnable, uNewEnable);

			data->aiCheckStatus[iSensorType] = NO_SENSOR_STATE;
			break;
		}

		data->aiCheckStatus[iSensorType] = RUNNING_SENSOR_STATE;

		break;
	case RUNNING_SENSOR_STATE:
		if (get_msdelay(dTempDelay)
			== get_msdelay(data->adDelayBuf[iSensorType]))
			break;

		ssp_dbg("[SSP]: %s - Change %u, New = %lldns\n",
			__func__, 1 << iSensorType, dNewDelay);

		memcpy(&uBuf[0], &dMsDelay, 4);
		memcpy(&uBuf[4], &maxBatchReportLatency, 4);
		uBuf[8] = batchOptions;
		send_instruction(data, CHANGE_DELAY, iSensorType, uBuf, 9);

		break;
	default:
		data->aiCheckStatus[iSensorType] = ADD_SENSOR_STATE;
	}
}

static void change_sensor_delay(struct ssp_data *data,
	int iSensorType, int64_t dNewDelay)
{
	u8 uBuf[9];
	s32 maxBatchReportLatency = 0;
	s8 batchOptions = 0;
	int64_t dTempDelay = data->adDelayBuf[iSensorType];
	s32 dMsDelay = get_msdelay(dNewDelay);

	// SUPPORT CAMERA SYNC ++++++
	if (iSensorType == GYROSCOPE_SENSOR) {
		if (dNewDelay == CAMERA_GYROSCOPE_SYNC) {
			dNewDelay = CAMERA_GYROSCOPE_SYNC_DELAY;
			dMsDelay = get_msdelay(dNewDelay);
			data->cameraGyroSyncMode = true;
		} else if(dNewDelay == CAMERA_GYROSCOPE_VDIS_SYNC){
			dNewDelay = CAMERA_GYROSCOPE_VDIS_SYNC_DELAY;
			dMsDelay = get_msdelay(dNewDelay);
			data->cameraGyroSyncMode = true;
		} else {
			data->cameraGyroSyncMode = false;
			if((data->adDelayBuf[iSensorType] == dNewDelay) && (data->aiCheckStatus[iSensorType] == RUNNING_SENSOR_STATE)) {
				pr_err("[SSP] same delay ignored! \n");
				return;
			}
		}

		if((data->cameraGyroSyncMode == true) && (data->aiCheckStatus[iSensorType] == RUNNING_SENSOR_STATE)){
			if(data->adDelayBuf[iSensorType] == dNewDelay)
				return;
		}
	}
	// SUPPORT CAMERA SYNC -----

	data->adDelayBuf[iSensorType] = dNewDelay;
	data->batchLatencyBuf[iSensorType] = maxBatchReportLatency;
	data->batchOptBuf[iSensorType] = batchOptions;

	switch (data->aiCheckStatus[iSensorType]) {
	case RUNNING_SENSOR_STATE:
		if (get_msdelay(dTempDelay)
			== get_msdelay(data->adDelayBuf[iSensorType]))
			break;

		ssp_dbg("[SSP]: %s - Change %u, New = %lldns\n",
			__func__, 1 << iSensorType, dNewDelay);

		memcpy(&uBuf[0], &dMsDelay, 4);
		memcpy(&uBuf[4], &maxBatchReportLatency, 4);
		uBuf[8] = batchOptions;
		send_instruction(data, CHANGE_DELAY, iSensorType, uBuf, 9);

		break;
	default:
		break;
	}
}

/*************************************************************************/
/* SSP data enable function                                              */
/*************************************************************************/

static int ssp_remove_sensor(struct ssp_data *data,
	unsigned int uChangedSensor, u64 uNewEnable)
{
	u8 uBuf[4];
	int64_t dSensorDelay = data->adDelayBuf[uChangedSensor];

	ssp_dbg("[SSP]: %s - remove sensor = %lld, current state = %lld\n",
		__func__, (u64)(1 << uChangedSensor), uNewEnable);

	data->adDelayBuf[uChangedSensor] = DEFUALT_POLLING_DELAY;
	data->batchLatencyBuf[uChangedSensor] = 0;
	data->batchOptBuf[uChangedSensor] = 0;

	if (uChangedSensor == ORIENTATION_SENSOR) {
		if (!(atomic64_read(&data->aSensorEnable)
			& (1 << ACCELEROMETER_SENSOR))) {
			uChangedSensor = ACCELEROMETER_SENSOR;
		} else {
			change_sensor_delay(data, ACCELEROMETER_SENSOR,
				data->adDelayBuf[ACCELEROMETER_SENSOR]);
			return 0;
		}
	} else if (uChangedSensor == ACCELEROMETER_SENSOR) {
		if (atomic64_read(&data->aSensorEnable)
			& (1 << ORIENTATION_SENSOR)) {
			change_sensor_delay(data, ORIENTATION_SENSOR,
				data->adDelayBuf[ORIENTATION_SENSOR]);
			return 0;
		}
	} else if (uChangedSensor == GYROSCOPE_SENSOR) {
		data->cameraGyroSyncMode = false;
	} else if(uChangedSensor == PROXIMITY_SENSOR)	{
		data->sensor_dump_flag_proximity = false;
	} else if(uChangedSensor == LIGHT_SENSOR)	{
		data->sensor_dump_flag_light = false;
	}

	if (!data->bSspShutdown)
		if (atomic64_read(&data->aSensorEnable) & (1 << uChangedSensor)) {
			s32 dMsDelay = get_msdelay(dSensorDelay);
			memcpy(&uBuf[0], &dMsDelay, 4);

			send_instruction(data, REMOVE_SENSOR,
				uChangedSensor, uBuf, 4);
		}
	data->aiCheckStatus[uChangedSensor] = NO_SENSOR_STATE;

	return 0;
}

/*************************************************************************/
/* ssp Sysfs                                                             */
/*************************************************************************/

static ssize_t show_enable_irq(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	ssp_dbg("[SSP]: %s - %d\n", __func__, !data->bSspShutdown);

	return sprintf(buf, "%d\n", !data->bSspShutdown);
}

static ssize_t set_enable_irq(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	u8 dTemp;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtou8(buf, 10, &dTemp) < 0)
		return -1;

	pr_info("[SSP] %s - %d start\n", __func__, dTemp);
	mutex_lock(&data->ssp_enable_mutex);
	if (dTemp) {
		reset_mcu(data);
		enable_debug_timer(data);
	} else if (!dTemp) {
		disable_debug_timer(data);
		ssp_enable(data, 0);
	} else
		pr_err("[SSP] %s - invalid value\n", __func__);
	mutex_unlock(&data->ssp_enable_mutex);
	pr_info("[SSP] %s - %d end\n", __func__, dTemp);
	return size;
}

static ssize_t show_sensors_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	ssp_dbg("[SSP]: %s - cur_enable = %llu\n", __func__,
		 (u64)(atomic64_read(&data->aSensorEnable)));

	return sprintf(buf, "%llu\n", (u64)(atomic64_read(&data->aSensorEnable)));
}

static ssize_t set_sensors_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dTemp;
	int iRet;
	u64 uNewEnable = 0;
	unsigned int uChangedSensor = 0;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dTemp) < 0)
		return -EINVAL;

	uNewEnable = (u64)dTemp;
	ssp_dbg("[SSP]: %s - new_enable = %llu, old_enable = %llu\n", __func__,
		 uNewEnable, (u64)atomic64_read(&data->aSensorEnable));

	if ((uNewEnable != atomic64_read(&data->aSensorEnable)) &&
		!(data->uSensorState & (uNewEnable - atomic64_read(&data->aSensorEnable)))) {
		pr_info("[SSP] %s - %llu is not connected(sensortate: 0x%llx)\n",
			__func__, uNewEnable - atomic64_read(&data->aSensorEnable), data->uSensorState);
		return -EINVAL;
	}

	if (uNewEnable == atomic64_read(&data->aSensorEnable))
		return size;

	mutex_lock(&data->enable_mutex);
	for (uChangedSensor = 0; uChangedSensor < SENSOR_MAX; uChangedSensor++) {
		if ((atomic64_read(&data->aSensorEnable) & (1 << uChangedSensor))
			!= (uNewEnable & (1 << uChangedSensor))) {

			if (!(uNewEnable & (1 << uChangedSensor))) {
				data->reportedData[uChangedSensor] = false;
				ssp_remove_sensor(data, uChangedSensor,
					uNewEnable); /* disable */
			} else { /* Change to ADD_SENSOR_STATE from KitKat */
				if (data->aiCheckStatus[uChangedSensor] == INITIALIZATION_STATE) {
					if (uChangedSensor == ACCELEROMETER_SENSOR) {
						accel_open_calibration(data);
						iRet = set_accel_cal(data);
						if (iRet < 0)
							pr_err("[SSP]: %s - set_accel_cal failed %d\n", __func__, iRet);
					}
					else if (uChangedSensor == PRESSURE_SENSOR)
						pressure_open_calibration(data);
					else if (uChangedSensor == PROXIMITY_SENSOR) {
#ifdef CONFIG_SENSORS_SSP_PROX_FACTORYCAL
						get_proximity_threshold(data);
						proximity_open_calibration(data);
#endif
						set_proximity_threshold(data);
					}
					else if(uChangedSensor == PROXIMITY_ALERT_SENSOR)
					{
						set_proximity_alert_threshold(data);
					}
#ifdef CONFIG_SENSORS_SSP_SX9306
					else if (uChangedSensor == GRIP_SENSOR) {
						open_grip_caldata(data);
						set_grip_calibration(data, true);
					}
#endif
				}
				data->aiCheckStatus[uChangedSensor] = ADD_SENSOR_STATE;
				enable_sensor(data, uChangedSensor, data->adDelayBuf[uChangedSensor]);
			}
			break;
		}
	}
	atomic64_set(&data->aSensorEnable, uNewEnable);
	mutex_unlock(&data->enable_mutex);

	return size;
}

static ssize_t set_flush(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dTemp;
	u8 sensor_type = 0;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dTemp) < 0)
		return -EINVAL;

	sensor_type = (u8)dTemp;
	if (!(atomic64_read(&data->aSensorEnable) & (1 << sensor_type)))
		return -EINVAL;

	if (flush(data, sensor_type) < 0) {
		pr_err("[SSP] ssp returns error for flush(%x)\n", sensor_type);
		return -EINVAL;
	}

	return size;
}

static ssize_t show_shake_cam(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	int enabled = 0;

	if ((atomic64_read(&data->aSensorEnable) & (1 << SHAKE_CAM)))
		enabled = 1;

	return sprintf(buf, "%d\n", enabled);
}

static ssize_t set_shake_cam(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	unsigned long enable = 0;
	unsigned char buffer[4];
	u64 uNewEnable;
	s32 dMsDelay = 20;

	if (strict_strtoul(buf, 10, &enable))
		return -EINVAL;

	memcpy(&buffer[0], &dMsDelay, 4);

	if (enable) {
		send_instruction(data, ADD_SENSOR, SHAKE_CAM,
				buffer, sizeof(buffer));
		uNewEnable =
			(u64)atomic64_read(&data->aSensorEnable)
			| ((u64)(1 << SHAKE_CAM));
	} else {
		send_instruction(data, REMOVE_SENSOR, SHAKE_CAM,
				buffer, sizeof(buffer));
		uNewEnable =
			(u64)atomic_read(&data->aSensorEnable)
			& (~(u64)(1 << SHAKE_CAM));
	}

	atomic64_set(&data->aSensorEnable, uNewEnable);
	return size;
}

static ssize_t show_acc_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[ACCELEROMETER_SENSOR]);
}

static ssize_t set_acc_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	if ((atomic64_read(&data->aSensorEnable) & (1 << ORIENTATION_SENSOR)) &&
		(data->adDelayBuf[ORIENTATION_SENSOR] < dNewDelay))
		data->adDelayBuf[ACCELEROMETER_SENSOR] = dNewDelay;
	else
		change_sensor_delay(data, ACCELEROMETER_SENSOR, dNewDelay);

	return size;
}

static ssize_t show_gyro_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[GYROSCOPE_SENSOR]);
}

static ssize_t set_gyro_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GYROSCOPE_SENSOR, dNewDelay);
	return size;
}

#ifdef CONFIG_SENSORS_SSP_INTERRUPT_GYRO_SENSOR
static ssize_t show_interrupt_gyro_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[INTERRUPT_GYRO_SENSOR]);
}

static ssize_t set_interrupt_gyro_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, INTERRUPT_GYRO_SENSOR, dNewDelay);
	return size;
}
#endif

static ssize_t show_mag_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[GEOMAGNETIC_SENSOR]);
}

static ssize_t set_mag_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GEOMAGNETIC_SENSOR, dNewDelay);

	return size;
}

static ssize_t show_uncal_mag_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
			data->adDelayBuf[GEOMAGNETIC_UNCALIB_SENSOR]);
}

static ssize_t set_uncal_mag_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GEOMAGNETIC_UNCALIB_SENSOR, dNewDelay);

	return size;
}

static ssize_t show_rot_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[ROTATION_VECTOR]);
}

static ssize_t set_rot_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, ROTATION_VECTOR, dNewDelay);

	return size;
}

static ssize_t show_game_rot_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[GAME_ROTATION_VECTOR]);
}

static ssize_t set_game_rot_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GAME_ROTATION_VECTOR, dNewDelay);

	return size;
}

static ssize_t show_step_det_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[STEP_DETECTOR]);
}

static ssize_t set_step_det_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -1;

	change_sensor_delay(data, STEP_DETECTOR, dNewDelay);
	return size;
}

static ssize_t show_sig_motion_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[SIG_MOTION_SENSOR]);
}

static ssize_t set_sig_motion_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -1;

	change_sensor_delay(data, SIG_MOTION_SENSOR, dNewDelay);
	return size;
}

static ssize_t show_step_cnt_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[STEP_COUNTER]);
}

static ssize_t set_step_cnt_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -1;

	change_sensor_delay(data, STEP_COUNTER, dNewDelay);
	return size;
}

static ssize_t show_uncalib_gyro_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[GYRO_UNCALIB_SENSOR]);
}

static ssize_t set_uncalib_gyro_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GYRO_UNCALIB_SENSOR, dNewDelay);

	return size;
}

static ssize_t show_pressure_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[PRESSURE_SENSOR]);
}

static ssize_t set_pressure_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, PRESSURE_SENSOR, dNewDelay);
	return size;
}

static ssize_t show_gesture_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[GESTURE_SENSOR]);
}

static ssize_t set_gesture_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, GESTURE_SENSOR, dNewDelay);

	return size;
}

static ssize_t show_light_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[LIGHT_SENSOR]);
}

static ssize_t set_light_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, LIGHT_SENSOR, dNewDelay);
	return size;
}

#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
static ssize_t show_light_ir_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[LIGHT_IR_SENSOR]);
}

static ssize_t set_light_ir_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, LIGHT_IR_SENSOR, dNewDelay);
	return size;
}
#endif

static ssize_t show_prox_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", data->adDelayBuf[PROXIMITY_SENSOR]);
}

static ssize_t set_prox_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, PROXIMITY_SENSOR, dNewDelay);
	return size;
}

static ssize_t show_temp_humi_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[TEMPERATURE_HUMIDITY_SENSOR]);
}

static ssize_t set_temp_humi_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -EINVAL;

	change_sensor_delay(data, TEMPERATURE_HUMIDITY_SENSOR, dNewDelay);
	return size;
}

static ssize_t show_tilt_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[TILT_DETECTOR]);
}

static ssize_t set_tilt_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -1;

	change_sensor_delay(data, TILT_DETECTOR, dNewDelay);
	return size;
}

static ssize_t show_pickup_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		data->adDelayBuf[PICKUP_GESTURE]);
}

static ssize_t set_pickup_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int64_t dNewDelay;
	struct ssp_data *data  = dev_get_drvdata(dev);

	if (kstrtoll(buf, 10, &dNewDelay) < 0)
		return -1;

	change_sensor_delay(data, PICKUP_GESTURE, dNewDelay);
	return size;
}

ssize_t ssp_sensorhub_voicel_pcmdump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	int status = ssp_sensorhub_pcm_dump(data->hub_data);

	return sprintf(buf, "%s\n", (status ? "OK" : "NG"));
}



static DEVICE_ATTR(voice_pcmdump, S_IRUGO,
	ssp_sensorhub_voicel_pcmdump_show, NULL);

static struct device_attribute *voice_attrs[] = {
	&dev_attr_voice_pcmdump,
	NULL,
};

static void initialize_voice_sysfs(struct ssp_data *data)
{
	sensors_register(data->voice_device, data, voice_attrs, "ssp_voice");
}

static void remove_voice_sysfs(struct ssp_data *data)
{
	sensors_unregister(data->voice_device, voice_attrs);
}
// for data injection
static ssize_t show_data_injection_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->data_injection_enable);
}

static ssize_t set_data_injection_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	struct ssp_msg *msg;
	unsigned int iRet;
	int64_t buffer;

	if (kstrtoll(buf, 10, &buffer) < 0)
		return -EINVAL;

	if (buffer != 1 && buffer != 0)
		return -EINVAL;

	data->data_injection_enable = buffer;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP]: %s - failed to allocate memory\n", __func__);
		return FAIL;
	}
	msg->cmd = MSG2SSP_AP_DATA_INJECTION_MODE_ON_OFF;
	msg->length = 1;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char *) kzalloc(1, GFP_KERNEL);
	if ((msg->buffer) == NULL) {
		pr_err("[SSP]: %s - failed to allocate memory\n", __func__);
		kfree(msg);
		return FAIL;
	}
	msg->free_buffer = 1;

	msg->buffer[0] = data->data_injection_enable;

	iRet = ssp_spi_async(data, msg);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - Data Injection Mode fail %d\n", __func__, iRet);
		return iRet;
	}

	pr_info("[SSP] %s Data Injection Mode Success %d data_injection_enable %d\n",
		__func__, iRet, data->data_injection_enable);
	return size;
}

static ssize_t show_sensor_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", data->sensor_state);
}

static ssize_t show_timestamp_factor(struct device *dev,
	struct device_attribute *attr, char *buf){
	struct ssp_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", data->timestamp_factor);
}

static ssize_t set_timestamp_factor(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	int64_t timestamp_factor = 0;

	if (kstrtoll(buf, 10, &timestamp_factor) < 0)
		return -1;

	data->timestamp_factor = (u64)timestamp_factor;
	return size;
}

//dev_attr_mcu_power
static ssize_t show_mcu_power(struct device *dev,
	struct device_attribute *attr, char *buf){
	struct ssp_data *data = dev_get_drvdata(dev);

	//pr_err("[SSP] %s++\n",__func__);

	if(data->regulator_vdd_mcu_1p8 != NULL){
		pr_err("[SSP] %s: vdd_mcu_1p8, is enabled %d\n", __func__, regulator_is_enabled(data->regulator_vdd_mcu_1p8));
		return sprintf(buf, "%d\n", regulator_is_enabled(data->regulator_vdd_mcu_1p8));
	} else if(data->shub_en >= 0){
		pr_err("[SSP] %s: shub_en(%d), is enabled %d\n", __func__, data->shub_en, gpio_get_value(data->shub_en));
		return sprintf(buf, "%d\n", gpio_get_value(data->shub_en));
	}
	return 0;
}

static ssize_t set_mcu_power(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	long long enable = 0;
	int ret = 0;

	//pr_err("[SSP] %s++\n",__func__);

	if (kstrtoll(buf, 8, &enable) < 0)
		return -1;

	if(data->regulator_vdd_mcu_1p8 != NULL){
		switch(enable){
			case 0:
				if (regulator_is_enabled(data->regulator_vdd_mcu_1p8)){
					ret = regulator_disable(data->regulator_vdd_mcu_1p8);
				}
				break;
			case 1:
				if (!regulator_is_enabled(data->regulator_vdd_mcu_1p8))
				ret = regulator_enable(data->regulator_vdd_mcu_1p8);
				if(ret)
					pr_err("[SSP] %s:failed to enable vdd_mcu_1p8(%d)\n", __func__, ret);
				break;
		}
		msleep(20);
		pr_err("[SSP] %s: enable vdd_mcu_1p8(%lld), is enabled %d\n", __func__, enable, regulator_is_enabled(data->regulator_vdd_mcu_1p8));
	}
	else if(data->shub_en >= 0){
		switch(enable){
			case 0:
				gpio_set_value(data->shub_en, 0);
				break;
			case 1:
				gpio_set_value(data->shub_en, 1);
				break;
		}
		msleep(20);
		pr_err("[SSP] %s: enable shub_en(%d), is enabled %d\n", __func__, data->shub_en, gpio_get_value(data->shub_en));
	}
	else{
		pr_err("[SSP] %s:no support to individual power source for sensorhub\n", __func__);
	}
	return size;
}

static ssize_t set_ssp_control(struct device *dev,
   struct device_attribute *attr, const char *buf, size_t size)
{
   printk("[SSP] SSP_CONTROL : %s \n", buf);

   if (strstr(buf, SSP_DEBUG_TIME_FLAG_ON)) {
	   ssp_debug_time_flag = true;
   } else if(strstr(buf, SSP_DEBUG_TIME_FLAG_OFF)){
	   ssp_debug_time_flag = false;
	}

   return size;
}


static ssize_t sensor_dump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data  = dev_get_drvdata(dev);
	int types[] = SENSOR_DUMP_SENSOR_LIST;
	char str_no_sensor_dump[] = "there is no sensor dump";
	int i=0, ret;
	char* sensor_dump;
	char temp[sensor_dump_length(DUMPREGISTER_MAX_SIZE)+LENGTH_SENSOR_TYPE_MAX+2]={0,};

	sensor_dump = (char*)kzalloc((sensor_dump_length(DUMPREGISTER_MAX_SIZE)+LENGTH_SENSOR_TYPE_MAX+3)*(sizeof(types)/sizeof(types[0])), GFP_KERNEL);

	for(i=0;i<sizeof(types)/sizeof(types[0]);i++)
	{

		if(data->sensor_dump[types[i]] != NULL)
		{
			snprintf(temp, (int)strlen(data->sensor_dump[types[i]])+LENGTH_SENSOR_TYPE_MAX+3,
				"%3d\n%s\n\n", types[i],data->sensor_dump[types[i]]); 			/* %3d -> 3 : LENGTH_SENSOR_TYPE_MAX */
			strcpy(&sensor_dump[(int)strlen(sensor_dump)],temp);
		}
	}

	if((int)strlen(sensor_dump) == 0)
		ret = snprintf(buf, (int)strlen(str_no_sensor_dump)+1, "%s\n",str_no_sensor_dump);
	else
		ret = snprintf(buf, (int)strlen(sensor_dump)+1, "%s\n", sensor_dump);

	kfree(sensor_dump);

	return ret;
}

static ssize_t sensor_dump_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data  = dev_get_drvdata(dev);
	int sensor_type, ret;
	char name[LENGTH_SENSOR_NAME_MAX+1] ={0,};

	sscanf(buf, "%30s", name);		/* 30 : LENGTH_SENSOR_NAME_MAX */

	if((strcmp(name, "all")) == 0)
	{
		ret = send_all_sensor_dump_command(data);
	}
	else
	{
		if(strcmp(name, "accelerometer") == 0)
			sensor_type = ACCELEROMETER_SENSOR;
		else if(strcmp(name, "gyroscope") == 0)
			sensor_type = GYROSCOPE_SENSOR;
		else if(strcmp(name, "magnetic") == 0)
			sensor_type = GEOMAGNETIC_UNCALIB_SENSOR;
		else if(strcmp(name, "pressure") == 0)
			sensor_type = PRESSURE_SENSOR;
		else if(strcmp(name, "proximity") == 0)
			sensor_type = PROXIMITY_SENSOR;
		else if(strcmp(name, "light") == 0)
			sensor_type = LIGHT_SENSOR;
		else
		{
			pr_err("[SSP] %s - is not supported : %s",__func__,buf);
			sensor_type = -1;
			return -EINVAL;
		}
		ret = send_sensor_dump_command(data,sensor_type);
	}

	return (ret == SUCCESS)? size : ret;
}

#if defined(CONFIG_SSP_REGISTER_RW)
int htoi(char input)
{
    int ret = 0;
    if('0' <= input && input <= '9')
        return ret = input - '0';
    else if('a'<= input && input <= 'f')
        return ret = input - 'a' + 10;
    else if('A' <= input && input <= 'F')
        return ret = input - 'A' + 10;
    else
        return 0;
}

int checkInputtedRegisterString(const char* string, char* CheckString[4])
{
    int ret = 0;
    int index = 0;
    char Inputstring[20] = {0, };
    char* Dupstring = NULL;

    memcpy(Inputstring, string, strlen(string));
    Dupstring = kstrdup(Inputstring, GFP_KERNEL);

    while((CheckString[index] = strsep(&Dupstring, " ")) != NULL)
    {
        u32 tmp = 0;
        switch(index)
        {
            case 0 :
                if (kstrtou32(&CheckString[index][0], 10, &tmp) < 0 ||((tmp < 0) || (tmp >= SENSOR_MAX)))
                {
                    pr_info("[SSP] %s invalid(%d)\n", __func__, tmp);
                    goto exit;
                }
                break;
            case 1 :
                if(CheckString[index][0] == 'r' || CheckString[index][0] == 'w')
                {
                   tmp = (CheckString[index][0] == 'w' ? 0 : 1);
                }
                else if (kstrtou32(&CheckString[index][0], 10, &tmp) < 0 || ((tmp != 0) && (tmp != 1)))
                {
                    pr_info("[SSP] %s invalid r/w\n", __func__);
                    goto exit;
                }
                break;
            case 2 :
            case 3 :
                if(CheckString[index][0] != '0' && CheckString[index][1] != 'x')
                {
                    pr_info("[SSP] %s invalid value(0xOO) %s\n", __func__, CheckString[index]);
                    goto exit;
                }
                tmp = (uint8_t)((htoi(CheckString[index][2])<< 4) | htoi(CheckString[index][3]));
                ret = index;
                break;
            default:
                ret = false;
                goto exit;
                break;
        }
        CheckString[index++][0] = tmp;
    }
    kfree(Dupstring);
return ret;
exit:
    ret = 0;
    kfree(Dupstring);
    pr_info("[SSP] %s - ret %d\n", __func__, ret);
return ret;
}

static ssize_t register_rw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
        struct ssp_data *data = dev_get_drvdata(dev);
        if(data->registerValue[1] == 1) // 1 is read
        {
            return sprintf(buf, "sensor(%d) %c regi(0x%x) val(0x%x) ret(%d)\n",
                                  data->registerValue[0], data->registerValue[1] == 1 ? 'r' : 'w',data->registerValue[2],data->registerValue[3],data->registerValue[4]);
        }
        else
        {
            if(data->registerValue[4] == true)
                return sprintf(buf, "sensor(%d) %c regi(0x%x) val(0x%x) SUCCESS\n",
                                  data->registerValue[0],  data->registerValue[1] == 1 ? 'r' : 'w',data->registerValue[2],data->registerValue[3]);
            else
                return sprintf(buf, "sensor(%d) %c regi(0x%x) val(0x%x) FAIL\n",
                                  data->registerValue[0],  data->registerValue[1] == 1 ? 'r' : 'w',data->registerValue[2],data->registerValue[3]);
        }
}

static ssize_t register_rw_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
        struct ssp_data *data = dev_get_drvdata(dev);
        struct ssp_msg *msg;
        int index = 0, iRet = 0;
        char* CheckString[4] = {0, };
        char sendBuff[5] = {0, };

        msg = kzalloc(sizeof(*msg), GFP_KERNEL);
        if (ZERO_OR_NULL_PTR(msg)) {
                pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
                return -ENOMEM;
        }

        iRet = checkInputtedRegisterString(buf, CheckString);

        if(iRet == 0) return -1;

        msg->cmd = MSG2SSP_AP_REGISTER_SETTING;
        msg->length = 5;
        msg->options = AP2HUB_READ;
        msg->data = 0;
        msg->buffer = sendBuff;
        msg->free_buffer = 0;

       for(index = 0; index <= iRet; index++)
                msg->data |= (u32)(CheckString[index][0] << (24 - 8*index));

        iRet = ssp_spi_sync(data, msg, 2000);

        if (iRet != SUCCESS)
            pr_err("[SSP] %s - fail %d\n", __func__, iRet);

        memcpy(data->registerValue, sendBuff, sizeof(sendBuff));
        return size;
}
static DEVICE_ATTR(register_rw,S_IRUGO | S_IWUSR | S_IWGRP,
	register_rw_show, register_rw_store);

#endif //CONFIG_SSP_REGISTER_RW

static DEVICE_ATTR(mcu_rev, S_IRUGO, mcu_revision_show, NULL);
static DEVICE_ATTR(mcu_name, S_IRUGO, mcu_model_name_show, NULL);
static DEVICE_ATTR(mcu_reset, S_IRUGO, mcu_reset_show, NULL);
static DEVICE_ATTR(mcu_dump, S_IRUGO, mcu_dump_show, NULL);

static DEVICE_ATTR(mcu_test, S_IRUGO | S_IWUSR | S_IWGRP,
	mcu_factorytest_show, mcu_factorytest_store);
static DEVICE_ATTR(mcu_sleep_test, S_IRUGO | S_IWUSR | S_IWGRP,
	mcu_sleep_factorytest_show, mcu_sleep_factorytest_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	show_sensors_enable, set_sensors_enable);
static DEVICE_ATTR(enable_irq, S_IRUGO | S_IWUSR | S_IWGRP,
	show_enable_irq, set_enable_irq);
static DEVICE_ATTR(accel_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_acc_delay, set_acc_delay);
static DEVICE_ATTR(gyro_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_gyro_delay, set_gyro_delay);
static DEVICE_ATTR(uncalib_gyro_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_uncalib_gyro_delay, set_uncalib_gyro_delay);
static DEVICE_ATTR(mag_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_mag_delay, set_mag_delay);
static DEVICE_ATTR(uncal_mag_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_uncal_mag_delay, set_uncal_mag_delay);
static DEVICE_ATTR(rot_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_rot_delay, set_rot_delay);
static DEVICE_ATTR(game_rot_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_game_rot_delay, set_game_rot_delay);
static DEVICE_ATTR(step_det_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_step_det_delay, set_step_det_delay);
static DEVICE_ATTR(pressure_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_pressure_delay, set_pressure_delay);
static DEVICE_ATTR(ssp_flush, S_IWUSR | S_IWGRP,
	NULL, set_flush);
static DEVICE_ATTR(shake_cam, S_IRUGO | S_IWUSR | S_IWGRP,
	show_shake_cam, set_shake_cam);
static DEVICE_ATTR(tilt_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_tilt_delay, set_tilt_delay);
static DEVICE_ATTR(pickup_poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_pickup_delay, set_pickup_delay);
static struct device_attribute dev_attr_gesture_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_gesture_delay, set_gesture_delay);
static struct device_attribute dev_attr_light_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_light_delay, set_light_delay);
#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
static struct device_attribute dev_attr_light_ir_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_light_ir_delay, set_light_ir_delay);
#endif
#ifdef CONFIG_SENSORS_SSP_INTERRUPT_GYRO_SENSOR
static struct device_attribute dev_attr_interrupt_gyro_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_interrupt_gyro_delay, set_interrupt_gyro_delay);
#endif
static struct device_attribute dev_attr_prox_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_prox_delay, set_prox_delay);
static struct device_attribute dev_attr_temp_humi_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_temp_humi_delay, set_temp_humi_delay);
static struct device_attribute dev_attr_sig_motion_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_sig_motion_delay, set_sig_motion_delay);
static struct device_attribute dev_attr_step_cnt_poll_delay
	= __ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	show_step_cnt_delay, set_step_cnt_delay);
// for data injection
static DEVICE_ATTR(data_injection_enable, S_IRUGO | S_IWUSR | S_IWGRP,
	show_data_injection_enable, set_data_injection_enable);

static DEVICE_ATTR(timestamp_factor, S_IRUGO | S_IWUSR | S_IWGRP,
	show_timestamp_factor, set_timestamp_factor);


static DEVICE_ATTR(ssp_control, S_IWUSR | S_IWGRP, NULL, set_ssp_control);


static DEVICE_ATTR(sensor_state, S_IRUGO, show_sensor_state, NULL);
static DEVICE_ATTR(mcu_power, 0664, show_mcu_power, set_mcu_power);

static DEVICE_ATTR(sensor_dump,S_IRUGO | S_IWUSR | S_IWGRP,
	sensor_dump_show, sensor_dump_store);

static struct device_attribute *mcu_attrs[] = {
	&dev_attr_enable,
	&dev_attr_mcu_rev,
	&dev_attr_mcu_name,
	&dev_attr_mcu_test,
	&dev_attr_mcu_reset,
	&dev_attr_mcu_dump,
	&dev_attr_mcu_sleep_test,
	&dev_attr_enable_irq,
	&dev_attr_accel_poll_delay,
	&dev_attr_gyro_poll_delay,
	&dev_attr_uncalib_gyro_poll_delay,
	&dev_attr_mag_poll_delay,
	&dev_attr_uncal_mag_poll_delay,
	&dev_attr_rot_poll_delay,
	&dev_attr_game_rot_poll_delay,
	&dev_attr_step_det_poll_delay,
	&dev_attr_pressure_poll_delay,
	&dev_attr_tilt_poll_delay,
	&dev_attr_pickup_poll_delay,
	&dev_attr_ssp_flush,
	&dev_attr_shake_cam,
	&dev_attr_data_injection_enable,
	&dev_attr_sensor_state,
	&dev_attr_timestamp_factor,
	&dev_attr_ssp_control,
	&dev_attr_sensor_dump,
#if defined(CONFIG_SSP_REGISTER_RW)
	&dev_attr_register_rw,
#endif
	NULL,
};

static long ssp_batch_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct ssp_data *data
		= container_of(file->private_data,
			struct ssp_data, batch_io_device);

	struct batch_config batch;

	void __user *argp = (void __user *)arg;
	int retries = 2;
	int ret = 0;
	int sensor_type, ms_delay;
	int timeout_ms = 0;
	u8 uBuf[9];

	sensor_type = (cmd & 0xFF);

	if(sensor_type >= SENSOR_MAX){
		pr_err("[SSP] Invalid sensor_type %d\n", sensor_type);
		return -EINVAL;
	}

	if ((cmd >> 8 & 0xFF) != BATCH_IOCTL_MAGIC) {
		pr_err("[SSP] Invalid BATCH CMD %x\n", cmd);
		return -EINVAL;
	}

	while (retries--) {
		ret = copy_from_user(&batch, argp, sizeof(batch));
		if (likely(!ret))
			break;
	}
	if (unlikely(ret)) {
		pr_err("[SSP] batch ioctl err(%d)\n", ret);
		return -EINVAL;
	}
	ms_delay = get_msdelay(batch.delay);
	timeout_ms = div_s64(batch.timeout, 1000000);
	memcpy(&uBuf[0], &ms_delay, 4);
	memcpy(&uBuf[4], &timeout_ms, 4);
	uBuf[8] = batch.flag;

	if (batch.timeout){ /* add or dry */

		if(!(batch.flag & SENSORS_BATCH_DRY_RUN)) { /* real batch, NOT DRY, change delay */
			ret = 1;
			/* if sensor is not running state, enable will be called.
			   MCU return fail when receive chage delay inst during NO_SENSOR STATE */
			if (data->aiCheckStatus[sensor_type] == RUNNING_SENSOR_STATE) {
				ret = send_instruction(data, CHANGE_DELAY, sensor_type, uBuf, 9);
			}
			if (ret > 0) { // ret 1 is success
				data->batchOptBuf[sensor_type] = (u8)batch.flag;
				data->batchLatencyBuf[sensor_type] = timeout_ms;
				data->adDelayBuf[sensor_type] = batch.delay;
			}
		} else { /* real batch, DRY RUN */
			ret = send_instruction(data, CHANGE_DELAY, sensor_type, uBuf, 9);
			if (ret > 0) { // ret 1 is success
				data->batchOptBuf[sensor_type] = (u8)batch.flag;
				data->batchLatencyBuf[sensor_type] = timeout_ms;
				data->adDelayBuf[sensor_type] = batch.delay;
			}
		}
	} else { /* remove batch or normal change delay, remove or add will be called. */

		if (!(batch.flag & SENSORS_BATCH_DRY_RUN)) { /* no batch, NOT DRY, change delay */
			data->batchOptBuf[sensor_type] = 0;
			data->batchLatencyBuf[sensor_type] = 0;
			data->adDelayBuf[sensor_type] = batch.delay;
			if (data->aiCheckStatus[sensor_type] == RUNNING_SENSOR_STATE) {
				send_instruction(data, CHANGE_DELAY, sensor_type, uBuf, 9);
			}
		}
	}

	pr_info("[SSP] batch %d: delay %lld, timeout %lld, flag %d, ret %d \n",
		sensor_type, batch.delay, batch.timeout, batch.flag, ret);
	if (!batch.timeout)
		return 0;
	if (ret <= 0)
		return -EINVAL;
	else
		return 0;
}


static struct file_operations ssp_batch_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.unlocked_ioctl = ssp_batch_ioctl,
};

static ssize_t ssp_data_injection_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct ssp_data *data
		= container_of(file->private_data,
			struct ssp_data, ssp_data_injection_device);

	struct ssp_msg *msg;
	int ret = 0;
	char *send_buffer;
	unsigned int sensor_type = 0;
	int x = 0;

	if (unlikely(count < 4)) {
		pr_err("[SSP] %s data length err(%d) \n", __func__, (u32)count);
		return -EINVAL;
	}

	send_buffer = kzalloc(count * sizeof(char), GFP_KERNEL);
	if (unlikely(!send_buffer)) {
		pr_err("[SSP] %s allocate memory for kernel buffer err \n", __func__);
		return -ENOMEM;
	}

	ret = copy_from_user(send_buffer, buf, count);
	if (unlikely(ret)) {
		pr_err("[SSP] %s memcpy for kernel buffer err \n", __func__);
		ret = -EFAULT;
		goto exit;
	}

	pr_info("[SSP] %s count %d endable %d \n", __func__, (u32)count, data->data_injection_enable);

	// sensorhub sensor type is enum, HAL layer sensor type is 1 << sensor_type. So it needs to change to enum format.
	for(sensor_type = 0; sensor_type < SENSOR_MAX; sensor_type++)
	{
		if(send_buffer[0] == (1 << sensor_type))
		{
			send_buffer[0] = sensor_type; // sensor type change to enum format.
			pr_info("[SSP] %s sensor_type = %d %d \n", __func__, sensor_type, (1 << sensor_type));
			break;
		}
		if(sensor_type == SENSOR_MAX - 1)
		{
			pr_info("[SSP] %s there in no sensor_type \n", __func__);
		}
	}

	for(x = 0; x < count; x++)
	{
		pr_info("[SSP] %s Data Injection : %d 0x%x\n", __func__, send_buffer[x], send_buffer[x]);
	}


	// injection data send to sensorhub.
	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP]: %s - failed to allocate memory\n", __func__);
		kfree(send_buffer);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_DATA_INJECTION_SEND;
	msg->length = count;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char *) kzalloc(count, GFP_KERNEL);
	if ((msg->buffer) == NULL) {
		pr_err("[SSP]: %s - failed to allocate memory\n", __func__);
		kfree(send_buffer);
		kfree(msg);
		return -ENOMEM;
	}
	msg->free_buffer = 1;

	memcpy(msg->buffer, send_buffer, count);

	ret = ssp_spi_async(data, msg);

	if (ret != SUCCESS) {
		pr_err("[SSP]: %s - Data Injection fail %d\n", __func__, ret);
		goto exit;
		//return ret;
	}

	pr_info("[SSP] %s Data Injection Success %d \n", __func__, ret);

exit:
	kfree(send_buffer);
	return ret >= 0 ? 0 : -1;
}

#if 0
static ssize_t ssp_data_injection_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{

	return 0;
}
#endif
static struct file_operations ssp_data_injection_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.write = ssp_data_injection_write,

};

static void initialize_mcu_factorytest(struct ssp_data *data)
{
	sensors_register(data->mcu_device, data, mcu_attrs, "ssp_sensor");
}

static void remove_mcu_factorytest(struct ssp_data *data)
{
	sensors_unregister(data->mcu_device, mcu_attrs);
}

int initialize_sysfs(struct ssp_data *data)
{
	struct device *sec_sensorhub_dev = sec_device_create(data, "sensorhub");
	device_create_file(sec_sensorhub_dev, &dev_attr_mcu_power);

	if (device_create_file(&data->gesture_input_dev->dev,
		&dev_attr_gesture_poll_delay))
		goto err_gesture_input_dev;

	if (device_create_file(&data->light_input_dev->dev,
		&dev_attr_light_poll_delay))
		goto err_light_input_dev;
#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
	if (device_create_file(&data->light_ir_input_dev->dev,
		&dev_attr_light_ir_poll_delay))
		goto err_light_ir_input_dev;
#endif
#ifdef CONFIG_SENSORS_SSP_INTERRUPT_GYRO_SENSOR
	if (device_create_file(&data->interrupt_gyro_input_dev->dev,
		&dev_attr_interrupt_gyro_poll_delay))
		goto err_interrupt_gyro_input_dev;
#endif
	if (device_create_file(&data->prox_input_dev->dev,
		&dev_attr_prox_poll_delay))
		goto err_prox_input_dev;

	if (device_create_file(&data->temp_humi_input_dev->dev,
		&dev_attr_temp_humi_poll_delay))
		goto err_temp_humi_input_dev;

	if (device_create_file(&data->sig_motion_input_dev->dev,
		&dev_attr_sig_motion_poll_delay))
		goto err_sig_motion_input_dev;

	if (device_create_file(&data->step_cnt_input_dev->dev,
		&dev_attr_step_cnt_poll_delay))
		goto err_step_cnt_input_dev;

	data->batch_io_device.minor = MISC_DYNAMIC_MINOR;
	data->batch_io_device.name = "batch_io";
	data->batch_io_device.fops = &ssp_batch_fops;
	if (misc_register(&data->batch_io_device))
		goto err_batch_io_dev;

	data->ssp_data_injection_device.minor = MISC_DYNAMIC_MINOR;
	data->ssp_data_injection_device.name = "ssp_data_injection";
	data->ssp_data_injection_device.fops = &ssp_data_injection_fops;
	if ((misc_register(&data->ssp_data_injection_device)) < 0)
		goto err_ssp_data_injection_device;

	initialize_accel_factorytest(data);
	initialize_gyro_factorytest(data);
	initialize_prox_factorytest(data);
	initialize_light_factorytest(data);
	initialize_pressure_factorytest(data);
	initialize_magnetic_factorytest(data);
	initialize_mcu_factorytest(data);
#ifdef CONFIG_SENSORS_SSP_TMG399x
	initialize_gesture_factorytest(data);
#endif

#ifdef CONFIG_SENSORS_SSP_IRLED
	initialize_irled_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_SHTC1
	initialize_temphumidity_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_MOBEAM
	initialize_mobeam(data);
#endif
#ifdef CONFIG_SENSORS_SSP_SX9306
	initialize_grip_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_LIGHT_COLORID
	initialize_hiddenhole_factorytest(data);
#endif
	/*snamy.jeong_0630 voice dump & data*/
	initialize_voice_sysfs(data);

	return SUCCESS;

err_ssp_data_injection_device:
	ssp_batch_fops.unlocked_ioctl = NULL;
	misc_deregister(&data->batch_io_device);
err_batch_io_dev:
	device_remove_file(&data->step_cnt_input_dev->dev,
		&dev_attr_step_cnt_poll_delay);
err_step_cnt_input_dev:
	device_remove_file(&data->sig_motion_input_dev->dev,
		&dev_attr_sig_motion_poll_delay);
err_sig_motion_input_dev:
	device_remove_file(&data->temp_humi_input_dev->dev,
		&dev_attr_temp_humi_poll_delay);
err_temp_humi_input_dev:
	device_remove_file(&data->prox_input_dev->dev,
		&dev_attr_prox_poll_delay);
err_prox_input_dev:
#ifdef CONFIG_SENSORS_SSP_INTERRUPT_GYRO_SENSOR
    device_remove_file(&data->interrupt_gyro_input_dev->dev,
        &dev_attr_interrupt_gyro_poll_delay);
err_interrupt_gyro_input_dev:
#endif
#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
	device_remove_file(&data->light_ir_input_dev->dev,
		&dev_attr_light_ir_poll_delay);
err_light_ir_input_dev:
#endif
	device_remove_file(&data->light_input_dev->dev,
		&dev_attr_light_poll_delay);
err_light_input_dev:
	device_remove_file(&data->gesture_input_dev->dev,
		&dev_attr_gesture_poll_delay);
err_gesture_input_dev:
	pr_err("[SSP] error init sysfs\n");
	return ERROR;
}

void remove_sysfs(struct ssp_data *data)
{
	device_remove_file(&data->gesture_input_dev->dev,
		&dev_attr_gesture_poll_delay);
	device_remove_file(&data->light_input_dev->dev,
		&dev_attr_light_poll_delay);
#ifdef CONFIG_SENSORS_SSP_IRDATA_FOR_CAMERA
	device_remove_file(&data->light_ir_input_dev->dev,
		&dev_attr_light_ir_poll_delay);
#endif
#ifdef CONFIG_SENSORS_SSP_INTERRUPT_GYRO_SENSOR
	device_remove_file(&data->interrupt_gyro_input_dev->dev,
		&dev_attr_interrupt_gyro_poll_delay);
#endif
	device_remove_file(&data->prox_input_dev->dev,
		&dev_attr_prox_poll_delay);
	device_remove_file(&data->temp_humi_input_dev->dev,
		&dev_attr_temp_humi_poll_delay);
	device_remove_file(&data->sig_motion_input_dev->dev,
		&dev_attr_sig_motion_poll_delay);
	device_remove_file(&data->step_cnt_input_dev->dev,
		&dev_attr_step_cnt_poll_delay);
	ssp_batch_fops.unlocked_ioctl = NULL;
	misc_deregister(&data->batch_io_device);

	ssp_data_injection_fops.write = NULL;
	misc_deregister(&data->ssp_data_injection_device);

	remove_accel_factorytest(data);
	remove_gyro_factorytest(data);
	remove_prox_factorytest(data);
	remove_light_factorytest(data);
	remove_pressure_factorytest(data);
	remove_magnetic_factorytest(data);
	remove_mcu_factorytest(data);
#ifdef CONFIG_SENSORS_SSP_TMG399x
	remove_gesture_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_IRLED
	remove_irled_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_SHTC1
	remove_temphumidity_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_MOBEAM
	remove_mobeam(data);
#endif
#ifdef CONFIG_SENSORS_SSP_SX9306
	remove_grip_factorytest(data);
#endif
#ifdef CONFIG_SENSORS_SSP_LIGHT_COLORID
	remove_hiddenhole_factorytest(data);
#endif
	/*snamy.jeong_0630 voice dump & data*/
	remove_voice_sysfs(data);

	destroy_sensor_class();
}
