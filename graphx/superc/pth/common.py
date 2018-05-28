
import os
import sys
import datetime

mpiexec_prefix = "mpiexec -ordered-output -prepend-rank "

# -------------------
# Environment agnostic
# -------------------

def ensure_dir_exists(dir):
    os.system("mkdir -p " + dir)

def err(msg):
    sys.stderr.write(msg + "\n")

class Paths(object):
    def __init__(self):
        self.script_dir = os.path.dirname(os.path.realpath(sys.argv[0]))
        self.base_dir = self.script_dir
        self.log_dir = os.path.join(self.base_dir, "logs")

    def build_dir(self, type):
        return "cmake-build-{}".format(type)

    def abs_build_dir(self, type):
        return os.path.join(self.base_dir, self.build_dir(type))

cached_paths = None

def get_paths():
    if cached_paths is None:
        global cached_paths
        cached_paths = Paths()

    return cached_paths

def r(cmd):
    err(cmd)
    os.system(cmd)

# -------------------
# Meant for scheduler
# -------------------

def prepare_log_dir():
    p = get_paths()
    datetime_component = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = os.path.join(p.log_dir, datetime_component)
    ensure_dir_exists(log_dir)
    return log_dir

def run_batch_string(cmds,
                     node_count = 1,
                     tasks_per_node = 1,
                     mem_per_task = "1gb",
                     queue="plgrid-short",
                     log_prefix="framework",
                     time="00:20:00"):
    p = get_paths()
    script = os.path.join(p.script_dir, "executor.py")
    cmds_arg_str = ' "' + '" "'.join(cmds) + '"'
    work_dir = ' "{}"'.format(p.base_dir)

    cmd = ("sbatch"
    " -J framework"
    " -N " + str(node_count) +
    " --ntasks-per-node " + str(tasks_per_node) +
    " --mem-per-cpu " + mem_per_task +
    " --time " + time +
    " -A ccbmc6"
    " -p " + queue +
    " --output " + log_prefix + ".so"
    " --error " + log_prefix + ".se"
    " " + script + work_dir + cmds_arg_str)

    print cmd
    return cmd

def graphx_test_cli(log_dir):
    paths = get_paths()
    cmd = "spark-submit --class ClusterRunner {}/graphx-perf-comp*.jar".format(paths.base_dir)
    return cmd


# -------------------
# Meant for executor
# -------------------

def import_modules_string():
    return ["module load plgrid/apps/spark/2.0.1"]

def start_spark_cluster():
    return ["start-spark-cluster.sh"]

def stop_spark_cluster():
    return ["stop-spark-cluster.sh"]

def only_on_master(cmds):
    cmd = "; ".join(cmds)
    return ["if [ $SLURM_NODEID -eq 0 ]; then {}; fi".format(cmd)]

def run_commands(cmds):
    cmd = "; ".join(cmds)
    r(cmd)

def get_workdir():
    return sys.argv[1]

def get_cmds():
    return sys.argv[2:]